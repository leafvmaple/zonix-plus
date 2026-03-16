/**
 * @file virtio_kbd.cpp
 * @brief Virtio-input keyboard driver (modern PCI transport).
 *
 * Provides GUI keyboard input via virtio-keyboard-pci on QEMU virt.
 * Uses VirtIO 1.x modern PCI transport with capability-based BAR regions,
 * the same approach as virtio_gpu.cpp.
 *
 * virtio-keyboard-pci (device ID 0x1052 = 0x1040+18) is a modern-only
 * device — it does NOT support the legacy BAR0 register layout.
 *
 * Virtio-input device has two virtqueues:
 *   VQ 0 (eventq): device → driver — delivers input_event structs
 *   VQ 1 (statusq): driver → device — for LED status (unused)
 *
 * Each event is: { le16 type, le16 code, le32 value } = 8 bytes.
 * We only care about EV_KEY (type=1) with value=1 (key press).
 */

#include "virtio_kbd.h"
#include "pci.h"
#include "gic.h"
#include "cons/cons.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include <asm/page.h>
#include <asm/mmu.h>

namespace {

// ============================================================================
// VirtIO constants
// ============================================================================

constexpr uint16_t VIRTIO_VENDOR = 0x1AF4;
constexpr uint16_t VIRTIO_INPUT_DEV = 0x1052;  // 0x1040 + 18

constexpr uint8_t VIRTIO_STATUS_ACK = 1;
constexpr uint8_t VIRTIO_STATUS_DRIVER = 2;
constexpr uint8_t VIRTIO_STATUS_DRIVER_OK = 4;
constexpr uint8_t VIRTIO_STATUS_FEATURES_OK = 8;

constexpr uint8_t VIRTIO_PCI_CAP_COMMON_CFG = 1;
constexpr uint8_t VIRTIO_PCI_CAP_NOTIFY_CFG = 2;
constexpr uint8_t VIRTIO_PCI_CAP_ISR_CFG = 3;

constexpr uint16_t VRING_DESC_F_WRITE = 2;

// Linux input event types
constexpr uint16_t EV_KEY = 1;

// ============================================================================
// VirtIO structures
// ============================================================================

struct VirtioInputEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

struct VringDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct VringAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct VringUsedElem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct VringUsed {
    uint16_t flags;
    uint16_t idx;
    VringUsedElem ring[];
} __attribute__((packed));

// ============================================================================
// Driver state
// ============================================================================

constexpr uint16_t MAX_QUEUE_SIZE = 64;

// Mapped register regions (from PCI capabilities)
volatile uint8_t* common_cfg = nullptr;
volatile uint8_t* notify_base = nullptr;
volatile uint8_t* isr_cfg = nullptr;
uint32_t notify_off_multiplier = 0;

// Eventq (VQ 0)
uint16_t vq_size = 0;
VringDesc* vq_desc = nullptr;
VringAvail* vq_avail = nullptr;
VringUsed* vq_used = nullptr;
uint16_t last_used_idx = 0;

// Event buffers — one per descriptor
VirtioInputEvent event_bufs[MAX_QUEUE_SIZE];

// Cached notify address (avoid reading common_cfg in interrupt context)
volatile uint16_t* vq_notify_addr = nullptr;

uint32_t s_gic_intid = 0;

// ============================================================================
// Common config register accessors (VirtIO 1.x §4.1.4.3)
// ============================================================================

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
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x08) = 0;
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x0C) = static_cast<uint32_t>(v);
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x08) = 1;
    *reinterpret_cast<volatile uint32_t*>(common_cfg + 0x0C) = static_cast<uint32_t>(v >> 32);
}

// ============================================================================
// PCI capability helpers (same pattern as virtio_gpu)
// ============================================================================

struct CapInfo {
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
};

bool find_cap(int bus, int dev, int func, uint8_t cap_type, CapInfo* out) {
    uint32_t reg = pci::config_read32(bus, dev, func, 0x04);
    if (!((reg >> 16) & (1 << 4)))
        return false;

    uint8_t ptr = pci::config_read32(bus, dev, func, pci::CAP_PTR) & 0xFF;
    while (ptr != 0) {
        uint32_t cap_hdr = pci::config_read32(bus, dev, func, ptr);
        uint8_t cap_id = cap_hdr & 0xFF;
        uint8_t cap_next = (cap_hdr >> 8) & 0xFF;

        if (cap_id == 0x09) {  // Vendor-specific (VirtIO)
            uint8_t cfg_type = static_cast<uint8_t>(cap_hdr >> 24);
            uint32_t w1 = pci::config_read32(bus, dev, func, ptr + 4);
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

uintptr_t map_bar_region(int bus, int dev, int func, const CapInfo* ci) {
    uint32_t bar_lo = pci::read_bar(bus, dev, func, ci->bar);
    uint64_t bar_phys = bar_lo & ~0xFULL;

    if ((bar_lo & 0x6) == 0x4) {
        uint32_t bar_hi = pci::read_bar(bus, dev, func, ci->bar + 1);
        bar_phys |= static_cast<uint64_t>(bar_hi) << 32;
    }

    uintptr_t phys = static_cast<uintptr_t>(bar_phys) + ci->offset;
    size_t map_size = ci->length;
    if (map_size < PG_SIZE)
        map_size = PG_SIZE;

    return vmm::mmio_map(phys, map_size, VM_WRITE | VM_NOCACHE);
}

// ============================================================================
// Linux keycode → ASCII translation (subset)
// ============================================================================

static const char keymap_normal[128] = {
    0,    0x1B, '1', '2',  '3',  '4', '5',  '6',   // 0-7
    '7',  '8',  '9', '0',  '-',  '=', '\b', '\t',  // 8-15
    'q',  'w',  'e', 'r',  't',  'y', 'u',  'i',   // 16-23
    'o',  'p',  '[', ']',  '\n', 0,   'a',  's',   // 24-31
    'd',  'f',  'g', 'h',  'j',  'k', 'l',  ';',   // 32-39
    '\'', '`',  0,   '\\', 'z',  'x', 'c',  'v',   // 40-47
    'b',  'n',  'm', ',',  '.',  '/', 0,    '*',   // 48-55
    0,    ' ',  0,   0,    0,    0,   0,    0,     // 56-63
    0,    0,    0,   0,    0,    0,   0,    '7',   // 64-71
    '8',  '9',  '-', '4',  '5',  '6', '+',  '1',   // 72-79
    '2',  '3',  '0', '.',  0,    0,   0,    0,     // 80-87
    0,    0,    0,   0,    0,    0,   0,    0,     // 88-95
    0,    0,    0,   0,    '\n', 0,   0,    0,     // 96-103 (96=KP_ENTER)
    0,    0,    0,   0,    0,    0,   0,    0,     // 104-111
    0,    0,    0,   0,    0,    0,   0,    0,     // 112-119
    0,    0,    0,   0,    0,    0,   0,    0,     // 120-127
};

// ============================================================================
// Virtqueue helpers
// ============================================================================

void fill_eventq() {
    for (uint16_t i = 0; i < vq_size; i++) {
        vq_desc[i].addr = virt_to_phys(&event_bufs[i]);
        vq_desc[i].len = sizeof(VirtioInputEvent);
        vq_desc[i].flags = VRING_DESC_F_WRITE;
        vq_desc[i].next = 0;

        vq_avail->ring[i] = i;
    }
    __asm__ volatile("dmb ishst" ::: "memory");
    vq_avail->idx = vq_size;
    __asm__ volatile("dmb ishst" ::: "memory");
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

namespace virtio_kbd {

int init() {
    int bus, dev, func;

    if (!pci::find_by_id(VIRTIO_VENDOR, VIRTIO_INPUT_DEV, &bus, &dev, &func)) {
        cprintf("virtio_kbd: device not found\n");
        return -1;
    }

    cprintf("virtio_kbd: found at PCI %d:%d.%d\n", bus, dev, func);
    pci::enable_bus_master(bus, dev, func);

    // Ensure PCI INTx is not disabled (bit 10 of Command register)
    uint32_t cmd = pci::config_read32(bus, dev, func, pci::COMMAND);
    cmd &= ~(1u << 10);  // clear Interrupt Disable
    pci::config_write32(bus, dev, func, pci::COMMAND, cmd);

    // Walk PCI capabilities to find modern config regions
    CapInfo common_ci{}, notify_ci{}, isr_ci{};
    if (!find_cap(bus, dev, func, VIRTIO_PCI_CAP_COMMON_CFG, &common_ci)) {
        cprintf("virtio_kbd: common_cfg cap not found\n");
        return -1;
    }
    if (!find_cap(bus, dev, func, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify_ci)) {
        cprintf("virtio_kbd: notify cap not found\n");
        return -1;
    }
    find_cap(bus, dev, func, VIRTIO_PCI_CAP_ISR_CFG, &isr_ci);

    notify_off_multiplier = read_notify_multiplier(bus, dev, func);

    // Map BAR regions
    uintptr_t common_va = map_bar_region(bus, dev, func, &common_ci);
    uintptr_t notify_va = map_bar_region(bus, dev, func, &notify_ci);
    if (common_va == 0 || notify_va == 0) {
        cprintf("virtio_kbd: failed to map BAR regions\n");
        return -1;
    }
    common_cfg = reinterpret_cast<volatile uint8_t*>(common_va);
    notify_base = reinterpret_cast<volatile uint8_t*>(notify_va);

    if (isr_ci.length > 0) {
        uintptr_t isr_va = map_bar_region(bus, dev, func, &isr_ci);
        isr_cfg = reinterpret_cast<volatile uint8_t*>(isr_va);
    }

    // Device init sequence (VirtIO 1.x §3.1.1)
    write_status(0);  // reset
    write_status(read_status() | VIRTIO_STATUS_ACK);
    write_status(read_status() | VIRTIO_STATUS_DRIVER);

    // Accept VIRTIO_F_VERSION_1 (bit 32) — required for modern devices
    write_driver_features(1ULL << 32);
    write_status(read_status() | VIRTIO_STATUS_FEATURES_OK);

    if (!(read_status() & VIRTIO_STATUS_FEATURES_OK)) {
        cprintf("virtio_kbd: features negotiation failed\n");
        return -1;
    }

    // Set up eventq (VQ 0)
    select_queue(0);
    uint16_t qsz = read_queue_size();
    if (qsz == 0) {
        cprintf("virtio_kbd: eventq size is 0\n");
        return -1;
    }
    if (qsz > MAX_QUEUE_SIZE)
        qsz = MAX_QUEUE_SIZE;
    vq_size = qsz;
    write_queue_size(qsz);

    // Allocate separate page-aligned regions for desc, avail, used
    size_t desc_sz = sizeof(VringDesc) * qsz;
    size_t avail_sz = sizeof(uint16_t) * (3 + qsz);
    size_t used_sz = sizeof(uint16_t) * 3 + sizeof(VringUsedElem) * qsz;

    size_t desc_pages = (desc_sz + PG_SIZE - 1) / PG_SIZE;
    size_t avail_pages = (avail_sz + PG_SIZE - 1) / PG_SIZE;
    size_t used_pages = (used_sz + PG_SIZE - 1) / PG_SIZE;

    Page* dp = pmm::alloc_pages(desc_pages);
    Page* ap = pmm::alloc_pages(avail_pages);
    Page* up = pmm::alloc_pages(used_pages);
    if (!dp || !ap || !up) {
        cprintf("virtio_kbd: VQ alloc failed\n");
        return -1;
    }

    vq_desc = static_cast<VringDesc*>(pmm::page2kva(dp));
    vq_avail = static_cast<VringAvail*>(pmm::page2kva(ap));
    vq_used = static_cast<VringUsed*>(pmm::page2kva(up));

    memset(vq_desc, 0, desc_pages * PG_SIZE);
    memset(vq_avail, 0, avail_pages * PG_SIZE);
    memset(vq_used, 0, used_pages * PG_SIZE);

    // Tell device the physical addresses
    write_queue_desc(virt_to_phys(reinterpret_cast<uintptr_t>(vq_desc)));
    write_queue_avail(virt_to_phys(reinterpret_cast<uintptr_t>(vq_avail)));
    write_queue_used(virt_to_phys(reinterpret_cast<uintptr_t>(vq_used)));

    // Fill eventq with writable buffers
    fill_eventq();

    // Enable queue
    write_queue_enable(1);

    // Mark device ready
    write_status(read_status() | VIRTIO_STATUS_DRIVER_OK);

    // Cache notify address and kick the device
    select_queue(0);
    uint16_t q_notify_off = read_queue_notify_off();
    vq_notify_addr = reinterpret_cast<volatile uint16_t*>(notify_base + q_notify_off * notify_off_multiplier);
    *vq_notify_addr = 0;

    // Set up GIC interrupt
    // QEMU virt: PCI INTx → GIC SPI = (dev % 4 + pin - 1) % 4 + 3
    // Read interrupt pin (offset 0x3D, 1=INTA)
    uint32_t int_reg = pci::config_read32(bus, dev, func, 0x3C);
    uint8_t int_pin = (int_reg >> 8) & 0xFF;  // interrupt pin (1-based)
    if (int_pin == 0)
        int_pin = 1;  // default to INTA
    uint32_t spi = (dev % 4 + int_pin - 1) % 4 + 3;
    s_gic_intid = spi + 32;
    gic::enable(s_gic_intid);

    cprintf("virtio_kbd: ready, eventq=%d, GIC IntID=%d\n", qsz, s_gic_intid);
    return 0;
}

void intr() {
    // Read ISR to acknowledge interrupt (modern: via ISR cap region)
    if (isr_cfg) {
        (void)*reinterpret_cast<volatile uint8_t*>(isr_cfg);
    }

    // Process used ring entries
    __asm__ volatile("dmb ish" ::: "memory");
    while (last_used_idx != vq_used->idx) {
        uint16_t idx = last_used_idx % vq_size;
        uint32_t desc_id = vq_used->ring[idx].id;

        VirtioInputEvent* ev = &event_bufs[desc_id];

        if (ev->type == EV_KEY && ev->value == 1) {
            uint16_t code = ev->code;
            if (code < 128) {
                char c = keymap_normal[code];
                if (c != 0) {
                    cons::push_input(c);
                }
            }
        }

        // Recycle descriptor
        uint16_t avail_idx = vq_avail->idx % vq_size;
        vq_avail->ring[avail_idx] = static_cast<uint16_t>(desc_id);
        __asm__ volatile("dmb ishst" ::: "memory");
        vq_avail->idx++;
        __asm__ volatile("dmb ishst" ::: "memory");

        last_used_idx++;
    }

    // Notify device we recycled buffers (use cached address)
    if (vq_notify_addr)
        *vq_notify_addr = 0;
}

uint32_t gic_intid() {
    return s_gic_intid;
}

}  // namespace virtio_kbd
