/**
 * @file virtio_gpu.cpp
 * @brief VirtIO-GPU driver for AArch64 (QEMU virt machine).
 *
 * Implements just enough of the VirtIO 1.x (modern) PCI transport and
 * GPU device protocol to create a 2-D framebuffer scanout that the
 * fbcons console can render into.
 *
 * Protocol summary:
 *   1. Reset device, negotiate features, set DRIVER_OK.
 *   2. RESOURCE_CREATE_2D  — allocate a host-side pixel buffer.
 *   3. RESOURCE_ATTACH_BACKING — point it at a guest-physical page range.
 *   4. SET_SCANOUT          — attach the resource to scanout 0.
 *   5. On each flush:
 *        TRANSFER_TO_HOST_2D → RESOURCE_FLUSH
 *
 * For the console use-case we never flush explicitly; the guest writes
 * directly into the backing pages and QEMU's periodic display refresh
 * picks up the changes (good enough at 30 Hz).
 */

#include "virtio_gpu.h"
#include "pci.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include <asm/page.h>
#include <asm/mmu.h>

// ---------------------------------------------------------------------------
// VirtIO PCI capability & common configuration structures
// ---------------------------------------------------------------------------

namespace {

// VirtIO PCI vendor/device
constexpr uint16_t VIRTIO_VENDOR = 0x1AF4;
constexpr uint16_t VIRTIO_GPU_DEV = 0x1050;  // transitional: 0x1040+0x10

// VirtIO device status bits
constexpr uint8_t VIRTIO_STATUS_ACK = 1;
constexpr uint8_t VIRTIO_STATUS_DRIVER = 2;
constexpr uint8_t VIRTIO_STATUS_DRIVER_OK = 4;
constexpr uint8_t VIRTIO_STATUS_FEATURES_OK = 8;

// VirtIO PCI capability types
constexpr uint8_t VIRTIO_PCI_CAP_COMMON_CFG = 1;
constexpr uint8_t VIRTIO_PCI_CAP_NOTIFY_CFG = 2;
constexpr uint8_t VIRTIO_PCI_CAP_ISR_CFG = 3;
constexpr uint8_t VIRTIO_PCI_CAP_DEVICE_CFG = 4;

// VirtIO-GPU command types
enum GpuCmd : uint32_t {
    CMD_GET_DISPLAY_INFO = 0x0100,
    CMD_RESOURCE_CREATE_2D = 0x0101,
    CMD_RESOURCE_UNREF = 0x0102,
    CMD_SET_SCANOUT = 0x0103,
    CMD_RESOURCE_FLUSH = 0x0104,
    CMD_TRANSFER_TO_HOST_2D = 0x0105,
    CMD_RESOURCE_ATTACH_BACKING = 0x0106,
    RESP_OK_NODATA = 0x1100,
    RESP_OK_DISPLAY_INFO = 0x1101,
};

// VirtIO-GPU pixel formats
constexpr uint32_t FORMAT_B8G8R8X8_UNORM = 2;

// GPU control header (prefix of every command/response)
struct GpuCtrlHdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
};

struct GpuRect {
    uint32_t x, y, width, height;
};

struct GpuDisplayInfo {
    GpuCtrlHdr hdr;
    struct {
        GpuRect r;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[16];
};

struct GpuResourceCreate2d {
    GpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

struct GpuResourceAttachBacking {
    GpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
};

struct GpuMemEntry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
};

struct GpuSetScanout {
    GpuCtrlHdr hdr;
    GpuRect r;
    uint32_t scanout_id;
    uint32_t resource_id;
};

struct GpuTransferToHost2d {
    GpuCtrlHdr hdr;
    GpuRect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
};

struct GpuResourceFlush {
    GpuCtrlHdr hdr;
    GpuRect r;
    uint32_t resource_id;
    uint32_t padding;
};

// ---------------------------------------------------------------------------
// VirtQueue (split virtqueue, single descriptor chain)
// ---------------------------------------------------------------------------

struct VringDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

constexpr uint16_t VRING_DESC_F_NEXT = 1;
constexpr uint16_t VRING_DESC_F_WRITE = 2;

struct VringAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];  // flexible array of queue_size entries
};

struct VringUsedElem {
    uint32_t id;
    uint32_t len;
};

struct VringUsed {
    uint16_t flags;
    uint16_t idx;
    VringUsedElem ring[];
};

// A simple single-queue abstraction
struct Virtqueue {
    uint16_t size;
    uint16_t free_head;
    uint16_t last_used_idx;

    VringDesc* desc;
    VringAvail* avail;
    VringUsed* used;

    // Physical addresses (for device)
    uintptr_t desc_phys;
    uintptr_t avail_phys;
    uintptr_t used_phys;
};

// Mapped register regions
volatile uint8_t* common_cfg = nullptr;   // VirtIO common config
volatile uint8_t* notify_base = nullptr;  // Notification region
volatile uint8_t* device_cfg = nullptr;   // Device-specific config
uint32_t notify_off_multiplier = 0;

// Control virtqueue (index 0) — used for all GPU commands
Virtqueue controlq{};

// Framebuffer backing memory
uintptr_t fb_phys = 0;
uintptr_t fb_virt = 0;
uint32_t fb_width = 0;
uint32_t fb_height = 0;
uint32_t fb_pitch = 0;
uint32_t fb_size = 0;

// ---------------------------------------------------------------------------
// Common config register accessors (offsets per VirtIO 1.x spec §4.1.4.3)
// ---------------------------------------------------------------------------

uint8_t read_status() {
    return *reinterpret_cast<volatile uint8_t*>(common_cfg + 0x14);
}
void write_status(uint8_t v) {
    *reinterpret_cast<volatile uint8_t*>(common_cfg + 0x14) = v;
}
void select_queue(uint16_t q) {
    *reinterpret_cast<volatile uint16_t*>(common_cfg + 0x16) = q;
}
uint16_t read_queue_size() {
    return *reinterpret_cast<volatile uint16_t*>(common_cfg + 0x18);
}
void write_queue_size(uint16_t v) {
    *reinterpret_cast<volatile uint16_t*>(common_cfg + 0x18) = v;
}
void write_queue_enable(uint16_t v) {
    *reinterpret_cast<volatile uint16_t*>(common_cfg + 0x1C) = v;
}
uint16_t read_queue_notify_off() {
    return *reinterpret_cast<volatile uint16_t*>(common_cfg + 0x1E);
}

void write_queue_desc(uint64_t v) {
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x20) = static_cast<uint32_t>(v);
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x24) = static_cast<uint32_t>(v >> 32);
}
void write_queue_avail(uint64_t v) {
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x28) = static_cast<uint32_t>(v);
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x2C) = static_cast<uint32_t>(v >> 32);
}
void write_queue_used(uint64_t v) {
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x30) = static_cast<uint32_t>(v);
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x34) = static_cast<uint32_t>(v >> 32);
}

void write_driver_features(uint64_t v) {
    // Select feature word 0 and write low 32 bits
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x08) = 0;                         // driver_feature_select
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x0C) = static_cast<uint32_t>(v);  // driver_feature
    // Select feature word 1 and write high 32 bits
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x08) = 1;
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x0C) = static_cast<uint32_t>(v >> 32);
}

// ---------------------------------------------------------------------------
// Virtqueue helpers
// ---------------------------------------------------------------------------

int vq_alloc(Virtqueue* vq, uint16_t qsize) {
    vq->size = qsize;
    vq->free_head = 0;
    vq->last_used_idx = 0;

    // Compute sizes
    size_t desc_sz = sizeof(VringDesc) * qsize;
    size_t avail_sz = sizeof(uint16_t) * 3 + sizeof(uint16_t) * qsize;
    size_t used_sz = sizeof(uint16_t) * 3 + sizeof(VringUsedElem) * qsize;

    // Allocate contiguous regions for each ring section (keep it simple: one alloc each)
    size_t desc_pages = (desc_sz + PG_SIZE - 1) / PG_SIZE;
    size_t avail_pages = (avail_sz + PG_SIZE - 1) / PG_SIZE;
    size_t used_pages = (used_sz + PG_SIZE - 1) / PG_SIZE;

    vq->desc = static_cast<VringDesc*>(kmalloc(desc_pages * PG_SIZE));
    vq->avail = static_cast<VringAvail*>(kmalloc(avail_pages * PG_SIZE));
    vq->used = static_cast<VringUsed*>(kmalloc(used_pages * PG_SIZE));
    if (!vq->desc || !vq->avail || !vq->used) {
        kfree(vq->desc);
        kfree(vq->avail);
        kfree(vq->used);
        vq->desc = nullptr;
        vq->avail = nullptr;
        vq->used = nullptr;
        cprintf("virtio_gpu: vq_alloc failed\n");
        return -1;
    }

    vq->desc_phys = virt_to_phys(reinterpret_cast<uintptr_t>(vq->desc));
    vq->avail_phys = virt_to_phys(reinterpret_cast<uintptr_t>(vq->avail));
    vq->used_phys = virt_to_phys(reinterpret_cast<uintptr_t>(vq->used));

    memset(vq->desc, 0, desc_pages * PG_SIZE);
    memset(vq->avail, 0, avail_pages * PG_SIZE);
    memset(vq->used, 0, used_pages * PG_SIZE);

    // Build free descriptor chain
    for (uint16_t i = 0; i < qsize - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[qsize - 1].next = 0;

    return 0;
}

// Submit a 2-descriptor chain: [request (device-readable)] → [response (device-writable)]
void vq_send_cmd(Virtqueue* vq, void* req, uint32_t req_len, void* resp, uint32_t resp_len) {
    uint16_t head = vq->free_head;
    uint16_t d0 = head;
    uint16_t d1 = vq->desc[d0].next;
    vq->free_head = vq->desc[d1].next;

    // Descriptor 0: request (device reads)
    vq->desc[d0].addr = virt_to_phys(reinterpret_cast<uintptr_t>(req));
    vq->desc[d0].len = req_len;
    vq->desc[d0].flags = VRING_DESC_F_NEXT;
    vq->desc[d0].next = d1;

    // Descriptor 1: response (device writes)
    vq->desc[d1].addr = virt_to_phys(reinterpret_cast<uintptr_t>(resp));
    vq->desc[d1].len = resp_len;
    vq->desc[d1].flags = VRING_DESC_F_WRITE;
    vq->desc[d1].next = 0;

    // Add to available ring
    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->size] = head;
    __asm__ volatile("dmb oshst" ::: "memory");  // store barrier
    vq->avail->idx = avail_idx + 1;

    // Notify device — write queue index to the notification address
    uint16_t notify_off = read_queue_notify_off();
    volatile uint16_t* notify_addr =
        reinterpret_cast<volatile uint16_t*>(notify_base + notify_off * notify_off_multiplier);
    *notify_addr = 0;  // queue index 0
}

// Poll until device processes the command
void vq_wait(Virtqueue* vq) {
    while (vq->used->idx == vq->last_used_idx) {
        __asm__ volatile("dmb osh" ::: "memory");
    }
    // Reclaim descriptors
    while (vq->last_used_idx != vq->used->idx) {
        uint16_t used_slot = vq->last_used_idx % vq->size;
        uint16_t desc_id = static_cast<uint16_t>(vq->used->ring[used_slot].id);
        // Return 2-descriptor chain to free list
        uint16_t d1 = vq->desc[desc_id].next;
        vq->desc[d1].next = vq->free_head;
        vq->free_head = desc_id;
        vq->last_used_idx++;
    }
}

// Send a GPU command and wait for response
void gpu_cmd(void* req, uint32_t req_len, void* resp, uint32_t resp_len) {
    select_queue(0);
    vq_send_cmd(&controlq, req, req_len, resp, resp_len);
    vq_wait(&controlq);
}

// ---------------------------------------------------------------------------
// PCI capability walking — find BAR regions for VirtIO modern transport
// ---------------------------------------------------------------------------

struct CapInfo {
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
};

bool find_cap(int bus, int dev, int func, uint8_t cap_type, CapInfo* out) {
    uint32_t reg = pci::config_read32(bus, dev, func, 0x04);
    // Check status bit 4 (Capabilities List)
    if (!((reg >> 16) & (1 << 4)))
        return false;

    uint8_t ptr = pci::config_read32(bus, dev, func, pci::CAP_PTR) & 0xFF;
    while (ptr != 0) {
        uint32_t cap_hdr = pci::config_read32(bus, dev, func, ptr);
        uint8_t cap_id = cap_hdr & 0xFF;
        uint8_t cap_next = (cap_hdr >> 8) & 0xFF;

        if (cap_id == 0x09) {  // Vendor-specific (VirtIO uses this)
            // VirtIO PCI cap: cap_hdr[ptr+3] = cfg_type, [ptr+4] = bar,
            //                  [ptr+8] = offset, [ptr+12] = length
            uint32_t w1 = pci::config_read32(bus, dev, func, ptr + 4);
            uint8_t cfg_type = (pci::config_read32(bus, dev, func, ptr) >> 24) & 0xFF;
            // Actually cfg_type is at ptr+3 in the capability
            cfg_type = static_cast<uint8_t>(cap_hdr >> 24);
            // Correct reading: virtio pci cap layout (bytes):
            // +0: cap_id(8), cap_next(8), cap_len(8), cfg_type(8)
            // +4: bar(8), padding(24)
            // +8: offset(32)
            // +12: length(32)
            uint8_t bar = w1 & 0xFF;
            uint32_t bar_offset = pci::config_read32(bus, dev, func, ptr + 8);
            uint32_t bar_length = pci::config_read32(bus, dev, func, ptr + 12);

            if (cfg_type == cap_type) {
                out->bar = bar;
                out->offset = bar_offset;
                out->length = bar_length;
                return true;
            }
        }
        ptr = cap_next;
    }
    return false;
}

// Read the notify_off_multiplier from the NOTIFY cap (at +16 in that cap)
uint32_t read_notify_multiplier(int bus, int dev, int func) {
    uint8_t ptr = pci::config_read32(bus, dev, func, pci::CAP_PTR) & 0xFF;
    while (ptr != 0) {
        uint32_t cap_hdr = pci::config_read32(bus, dev, func, ptr);
        uint8_t cap_id = cap_hdr & 0xFF;
        uint8_t cap_next = (cap_hdr >> 8) & 0xFF;
        uint8_t cfg_type = static_cast<uint8_t>(cap_hdr >> 24);

        if (cap_id == 0x09 && cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
            return pci::config_read32(bus, dev, func, ptr + 16);
        }
        ptr = cap_next;
    }
    return 0;
}

// Map a BAR + offset region into kernel virtual space
uintptr_t map_bar_region(int bus, int dev, int func, const CapInfo* ci) {
    uint32_t bar_lo = pci::read_bar(bus, dev, func, ci->bar);
    uint64_t bar_phys = bar_lo & ~0xFULL;

    // Check if 64-bit BAR
    if ((bar_lo & 0x6) == 0x4) {
        uint32_t bar_hi = pci::read_bar(bus, dev, func, ci->bar + 1);
        bar_phys |= static_cast<uint64_t>(bar_hi) << 32;
    }

    uintptr_t phys = static_cast<uintptr_t>(bar_phys) + ci->offset;
    size_t map_size = ci->length;
    if (map_size < PG_SIZE)
        map_size = PG_SIZE;

    uintptr_t va = vmm::mmio_map(phys, map_size, VM_WRITE | VM_NOCACHE);
    return va;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace virtio_gpu {

int init() {
    // 1. Find the device on PCI bus
    int bus, dev, func;
    if (!pci::find_by_id(VIRTIO_VENDOR, VIRTIO_GPU_DEV, &bus, &dev, &func)) {
        cprintf("virtio_gpu: device not found on PCI bus\n");
        return -1;
    }
    cprintf("virtio_gpu: found at PCI %02d:%02d.%d\n", bus, dev, func);

    pci::enable_bus_master(bus, dev, func);

    // 2. Walk PCI capabilities to find config regions
    CapInfo common_ci{}, notify_ci{}, device_ci{};
    if (!find_cap(bus, dev, func, VIRTIO_PCI_CAP_COMMON_CFG, &common_ci)) {
        cprintf("virtio_gpu: common cfg capability not found\n");
        return -1;
    }
    if (!find_cap(bus, dev, func, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify_ci)) {
        cprintf("virtio_gpu: notify capability not found\n");
        return -1;
    }
    find_cap(bus, dev, func, VIRTIO_PCI_CAP_DEVICE_CFG, &device_ci);

    notify_off_multiplier = read_notify_multiplier(bus, dev, func);

    // 3. Map BAR regions
    uintptr_t common_va = map_bar_region(bus, dev, func, &common_ci);
    uintptr_t notify_va = map_bar_region(bus, dev, func, &notify_ci);
    if (common_va == 0 || notify_va == 0) {
        cprintf("virtio_gpu: failed to map BAR regions\n");
        return -1;
    }
    common_cfg = reinterpret_cast<volatile uint8_t*>(common_va);
    notify_base = reinterpret_cast<volatile uint8_t*>(notify_va);
    if (device_ci.length > 0) {
        uintptr_t dev_va = map_bar_region(bus, dev, func, &device_ci);
        device_cfg = reinterpret_cast<volatile uint8_t*>(dev_va);
    }

    cprintf("virtio_gpu: common_cfg=0x%lx notify=0x%lx\n", static_cast<unsigned long>(common_va),
            static_cast<unsigned long>(notify_va));

    // 4. Device initialisation sequence (VirtIO 1.x §3.1.1)
    write_status(0);  // reset
    write_status(read_status() | VIRTIO_STATUS_ACK);
    write_status(read_status() | VIRTIO_STATUS_DRIVER);
    // Accept VIRTIO_F_VERSION_1 (bit 32) — required for modern devices
    write_driver_features(1ULL << 32);
    write_status(read_status() | VIRTIO_STATUS_FEATURES_OK);

    if (!(read_status() & VIRTIO_STATUS_FEATURES_OK)) {
        cprintf("virtio_gpu: features negotiation failed\n");
        return -1;
    }

    // 5. Set up controlq (queue index 0)
    select_queue(0);
    uint16_t qsize = read_queue_size();
    if (qsize == 0) {
        cprintf("virtio_gpu: controlq size is 0\n");
        return -1;
    }
    if (qsize > 256)
        qsize = 256;  // cap for simplicity
    write_queue_size(qsize);

    if (vq_alloc(&controlq, qsize) != 0)
        return -1;

    write_queue_desc(controlq.desc_phys);
    write_queue_avail(controlq.avail_phys);
    write_queue_used(controlq.used_phys);
    write_queue_enable(1);

    // Mark device ready
    write_status(read_status() | VIRTIO_STATUS_DRIVER_OK);
    cprintf("virtio_gpu: device ready (qsize=%d)\n", qsize);

    // 6. GET_DISPLAY_INFO to learn native resolution
    GpuCtrlHdr get_info_cmd{};
    get_info_cmd.type = CMD_GET_DISPLAY_INFO;
    GpuDisplayInfo disp_info{};
    gpu_cmd(&get_info_cmd, sizeof(get_info_cmd), &disp_info, sizeof(disp_info));

    if (disp_info.hdr.type != RESP_OK_DISPLAY_INFO) {
        cprintf("virtio_gpu: GET_DISPLAY_INFO failed (type=0x%x)\n", disp_info.hdr.type);
        return -1;
    }

    fb_width = disp_info.pmodes[0].r.width;
    fb_height = disp_info.pmodes[0].r.height;
    if (fb_width == 0 || fb_height == 0) {
        cprintf("virtio_gpu: display reports 0×0 resolution\n");
        return -1;
    }
    fb_pitch = fb_width * 4;  // 32-bit BGRX
    fb_size = fb_pitch * fb_height;
    cprintf("virtio_gpu: display %dx%d\n", fb_width, fb_height);

    // 7. Allocate guest-physical framebuffer backing
    size_t fb_pages = (fb_size + PG_SIZE - 1) / PG_SIZE;
    void* fb_buf = kmalloc(fb_pages * PG_SIZE);
    if (!fb_buf) {
        cprintf("virtio_gpu: failed to allocate %lu framebuffer pages\n", static_cast<unsigned long>(fb_pages));
        return -1;
    }
    fb_virt = reinterpret_cast<uintptr_t>(fb_buf);
    fb_phys = virt_to_phys(fb_virt);
    memset(reinterpret_cast<void*>(fb_virt), 0, fb_pages * PG_SIZE);

    // 8. RESOURCE_CREATE_2D
    GpuResourceCreate2d create_cmd{};
    GpuCtrlHdr create_resp{};
    create_cmd.hdr.type = CMD_RESOURCE_CREATE_2D;
    create_cmd.resource_id = 1;
    create_cmd.format = FORMAT_B8G8R8X8_UNORM;
    create_cmd.width = fb_width;
    create_cmd.height = fb_height;
    gpu_cmd(&create_cmd, sizeof(create_cmd), &create_resp, sizeof(create_resp));
    if (create_resp.type != RESP_OK_NODATA) {
        cprintf("virtio_gpu: RESOURCE_CREATE_2D failed (0x%x)\n", create_resp.type);
        return -1;
    }

    // 9. RESOURCE_ATTACH_BACKING — point resource at our framebuffer pages
    // We send the attach header followed immediately by one GpuMemEntry.
    // Pack them contiguously to form a single request buffer.
    struct [[gnu::packed]] {
        GpuResourceAttachBacking hdr;
        GpuMemEntry entry;
    } attach_cmd{};
    GpuCtrlHdr attach_resp{};

    attach_cmd.hdr.hdr.type = CMD_RESOURCE_ATTACH_BACKING;
    attach_cmd.hdr.resource_id = 1;
    attach_cmd.hdr.nr_entries = 1;
    attach_cmd.entry.addr = fb_phys;
    attach_cmd.entry.length = fb_size;
    gpu_cmd(&attach_cmd, sizeof(attach_cmd), &attach_resp, sizeof(attach_resp));
    if (attach_resp.type != RESP_OK_NODATA) {
        cprintf("virtio_gpu: ATTACH_BACKING failed (0x%x)\n", attach_resp.type);
        return -1;
    }

    // 10. SET_SCANOUT
    GpuSetScanout scanout_cmd{};
    GpuCtrlHdr scanout_resp{};
    scanout_cmd.hdr.type = CMD_SET_SCANOUT;
    scanout_cmd.r.x = 0;
    scanout_cmd.r.y = 0;
    scanout_cmd.r.width = fb_width;
    scanout_cmd.r.height = fb_height;
    scanout_cmd.scanout_id = 0;
    scanout_cmd.resource_id = 1;
    gpu_cmd(&scanout_cmd, sizeof(scanout_cmd), &scanout_resp, sizeof(scanout_resp));
    if (scanout_resp.type != RESP_OK_NODATA) {
        cprintf("virtio_gpu: SET_SCANOUT failed (0x%x)\n", scanout_resp.type);
        return -1;
    }

    // 11. Initial TRANSFER + FLUSH to show cleared screen
    GpuTransferToHost2d xfer_cmd{};
    GpuCtrlHdr xfer_resp{};
    xfer_cmd.hdr.type = CMD_TRANSFER_TO_HOST_2D;
    xfer_cmd.r.width = fb_width;
    xfer_cmd.r.height = fb_height;
    xfer_cmd.resource_id = 1;
    gpu_cmd(&xfer_cmd, sizeof(xfer_cmd), &xfer_resp, sizeof(xfer_resp));

    GpuResourceFlush flush_cmd{};
    GpuCtrlHdr flush_resp{};
    flush_cmd.hdr.type = CMD_RESOURCE_FLUSH;
    flush_cmd.r.width = fb_width;
    flush_cmd.r.height = fb_height;
    flush_cmd.resource_id = 1;
    gpu_cmd(&flush_cmd, sizeof(flush_cmd), &flush_resp, sizeof(flush_resp));

    cprintf("virtio_gpu: scanout active, fb at virt=0x%lx phys=0x%lx (%d KB)\n", static_cast<unsigned long>(fb_virt),
            static_cast<unsigned long>(fb_phys), fb_size / 1024);

    return 0;
}

void flush() {
    if (fb_width == 0 || fb_height == 0)
        return;

    GpuTransferToHost2d xfer{};
    GpuCtrlHdr xfer_resp{};
    xfer.hdr.type = CMD_TRANSFER_TO_HOST_2D;
    xfer.r.width = fb_width;
    xfer.r.height = fb_height;
    xfer.resource_id = 1;
    gpu_cmd(&xfer, sizeof(xfer), &xfer_resp, sizeof(xfer_resp));

    GpuResourceFlush fl{};
    GpuCtrlHdr fl_resp{};
    fl.hdr.type = CMD_RESOURCE_FLUSH;
    fl.r.width = fb_width;
    fl.r.height = fb_height;
    fl.resource_id = 1;
    gpu_cmd(&fl, sizeof(fl), &fl_resp, sizeof(fl_resp));
}

}  // namespace virtio_gpu

// Accessors for fbcons to retrieve framebuffer parameters
uintptr_t virtio_gpu_get_fb_virt() {
    return fb_virt;
}
uint32_t virtio_gpu_get_fb_width() {
    return fb_width;
}
uint32_t virtio_gpu_get_fb_height() {
    return fb_height;
}
uint32_t virtio_gpu_get_fb_pitch() {
    return fb_pitch;
}
