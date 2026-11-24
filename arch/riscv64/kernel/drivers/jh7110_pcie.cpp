// JH7110 PCIe0 bring-up: clocks and resets in the STG clock generator,
// root-port mode in the STG syscon, PERST# on GPIO26, then the PLDA bridge
// set up as a root port with outbound windows for configuration space and
// 32-bit memory. Device DMA reaches DRAM unchanged and is cache coherent.

#include "jh7110_pcie.h"
#include "timer.h"
#include "drivers/mmio.h"
#include "drivers/pci.h"
#include <kernel/log.h>

#include <asm/arch.h>
#include <asm/memlayout.h>

namespace {

constexpr uintptr_t SYSCRG_PHYS = 0x13020000;
constexpr uintptr_t STGCRG_PHYS = 0x10230000;
constexpr uintptr_t STG_SYSCON_PHYS = 0x10240000;
constexpr uintptr_t GPIO_PHYS = 0x13040000;
constexpr uintptr_t PHY0_PHYS = 0x10210000;
constexpr uintptr_t BRIDGE_PHYS = 0x2B000000;

constexpr uint32_t CLOCK_ENABLE = 1U << 31;
constexpr uintptr_t SYS_CLK_NOC_STG_AXI = 0x180;
constexpr uintptr_t STG_CLK_PCIE0_AXI_MST0 = 0x20;
constexpr uintptr_t STG_CLK_PCIE0_APB = 0x24;
constexpr uintptr_t STG_CLK_PCIE0_TL = 0x28;
constexpr uintptr_t STG_CLK_PCIE_SLV_MAIN = 0x38;
constexpr uintptr_t STG_RESET_ASSERT = 0x74;
constexpr uintptr_t STG_RESET_STATUS = 0x78;  // 1: released
constexpr uintptr_t STG_CLOCKS[] = {STG_CLK_PCIE_SLV_MAIN, STG_CLK_PCIE0_AXI_MST0, STG_CLK_PCIE0_APB, STG_CLK_PCIE0_TL};
constexpr uint32_t STG_RESETS_PCIE0 = 0x3FU << 11;  // AXI master/slaves, bridge, core, APB

// STG syscon, PCIe0 block.
constexpr uintptr_t SYSCON_AR = 0xC0;
constexpr uintptr_t SYSCON_AW = 0xC4;
constexpr uintptr_t SYSCON_RP_NEP = 0x130;
constexpr uintptr_t SYSCON_LINK_STATUS = 0x1B8;
constexpr uint32_t AR_FUNCTION_SHIFT = 17;
constexpr uint32_t AR_FUNCTION_MASK = 0xFU << AR_FUNCTION_SHIFT;
constexpr uint32_t AW_FUNCTION_SHIFT = 9;
constexpr uint32_t AW_FUNCTION_MASK = 0xFU << AW_FUNCTION_SHIFT;
constexpr uint32_t AW_CKREF_MASK = 0x3U << 18;
constexpr uint32_t AW_CKREF_INTERNAL = 0x2U << 18;
constexpr uint32_t AW_CLKREQ = 1U << 22;
constexpr uint32_t RP_NEP_ROOT_PORT = 1U << 8;
constexpr uint32_t LINK_DATA_ACTIVE = 1U << 5;

// PHY: the voltage-controlled oscillator tuning every driver applies.
constexpr uintptr_t PHY_KVCO_LEVEL = 0x28;
constexpr uintptr_t PHY_KVCO_TUNE = 0x80;
constexpr uint32_t KVCO_LEVEL = 0x91;
constexpr uint32_t KVCO_TUNE = 0x0C;

// GPIO26 drives PERST# (low holds the device in reset): one byte per pin.
constexpr uintptr_t GPIO_OUTPUT_ENABLE = 0x18;  // Pins 24-27; 0 enables the driver
constexpr uintptr_t GPIO_OUTPUT_VALUE = 0x58;
constexpr uint32_t PERST_SHIFT = 16;  // Pin 26 within its word
constexpr uint32_t PERST_ENABLE_MASK = 0x3FU << PERST_SHIFT;
constexpr uint32_t PERST_VALUE_MASK = 0x7FU << PERST_SHIFT;

// PLDA bridge registers.
constexpr uintptr_t BRIDGE_GEN_SETTINGS = 0x80;
constexpr uintptr_t BRIDGE_CLASS = 0x9C;
constexpr uintptr_t BRIDGE_MISC = 0xB4;
constexpr uintptr_t BRIDGE_PREFETCH = 0xFC;
constexpr uintptr_t BRIDGE_LTR = 0x3F0;
constexpr uintptr_t BRIDGE_INBOUND = 0x600;
constexpr uintptr_t BRIDGE_OUTBOUND = 0x800;
constexpr uintptr_t BRIDGE_ROOT_BAR0 = 0x1010;
constexpr uintptr_t BRIDGE_ROOT_BAR1 = 0x1014;
constexpr uint32_t GEN_ROOT_PORT = 1U << 0;
constexpr uint32_t MISC_FUNCTION_DISABLE = 1U << 15;
constexpr uint32_t CLASS_PCI_BRIDGE = 0x060400U << 8;
constexpr uint32_t PREFETCH_64BIT = 1U << 3;
constexpr uint32_t LTR_FORWARD = 1U << 2;
constexpr uint32_t INBOUND_PASS_ALL = 0x25U << 1;  // 2^38 bytes, untranslated

// Outbound windows: source address with size field 2^(n+1) and enable,
// translated address, then 1 for configuration or 0 for memory requests.
constexpr uintptr_t WINDOW_STRIDE = 0x20;
constexpr uint32_t WINDOW_ENABLE = 1;
constexpr uintptr_t MEMORY_PHYS = 0x30000000;
constexpr uint64_t CONFIG_WINDOW_BYTES = 16ULL << 20;
constexpr uint64_t MEMORY_WINDOW_BYTES = 128ULL << 20;

constexpr uint64_t PERST_HOLD_US = 100000;
constexpr uint64_t LINK_TIMEOUT_US = 1000000;
constexpr uint64_t LINK_SETTLE_US = 100000;
constexpr uint64_t RESET_TIMEOUT_US = 10000;

uintptr_t mapped(uintptr_t pa) {
    return pa + KERNEL_BASE;
}

uint64_t now_us() {
    return timer::now() * 1000000 / timer::TIMER_FREQ_HZ;
}

void delay_us(uint64_t us) {
    const uint64_t end = now_us() + us;
    while (now_us() < end)
        arch_spin_hint();
}

void update(uintptr_t base, uintptr_t offset, uint32_t clear, uint32_t set) {
    mmio::write32(base, offset, (mmio::read32(base, offset) & ~clear) | set);
}

void set_perst(bool released) {
    const uintptr_t gpio = mapped(GPIO_PHYS);
    update(gpio, GPIO_OUTPUT_VALUE, PERST_VALUE_MASK, released ? 1U << PERST_SHIFT : 0);
    update(gpio, GPIO_OUTPUT_ENABLE, PERST_ENABLE_MASK, 0);
}

uint32_t log2_size(uint64_t bytes) {
    uint32_t shift = 0;
    while ((1ULL << shift) < bytes)
        ++shift;
    return shift;
}

void program_window(int index, uint64_t source, uint64_t translated, uint64_t bytes, bool config) {
    const uintptr_t bridge = mapped(BRIDGE_PHYS);
    const uintptr_t entry = BRIDGE_OUTBOUND + index * WINDOW_STRIDE;
    mmio::write32(bridge, entry,
                  (static_cast<uint32_t>(source) & 0xFFFFF000) | ((log2_size(bytes) - 1) << 1) | WINDOW_ENABLE);
    mmio::write32(bridge, entry + 0x04, static_cast<uint32_t>(source >> 32));
    mmio::write32(bridge, entry + 0x08, static_cast<uint32_t>(translated));
    mmio::write32(bridge, entry + 0x0C, static_cast<uint32_t>(translated >> 32));
    mmio::write32(bridge, entry + 0x10, config ? 1 : 0);
}

// Disables PCIe functions 1-3 of the bridge, each selected through the
// syscon before its disable bit is set.
void disable_extra_functions() {
    const uintptr_t syscon = mapped(STG_SYSCON_PHYS);
    const uintptr_t bridge = mapped(BRIDGE_PHYS);
    for (uint32_t function = 1; function <= 3; ++function) {
        update(syscon, SYSCON_AR, AR_FUNCTION_MASK, function << AR_FUNCTION_SHIFT);
        update(syscon, SYSCON_AW, AW_FUNCTION_MASK, function << AW_FUNCTION_SHIFT);
        update(bridge, BRIDGE_MISC, 0, MISC_FUNCTION_DISABLE);
    }
    update(syscon, SYSCON_AR, AR_FUNCTION_MASK, 0);
    update(syscon, SYSCON_AW, AW_FUNCTION_MASK, 0);
}

}  // namespace

namespace jh7110_pcie {

Error start() {
    const uintptr_t syscon = mapped(STG_SYSCON_PHYS);
    const uintptr_t stg = mapped(STGCRG_PHYS);
    const uintptr_t bridge = mapped(BRIDGE_PHYS);

    const uintptr_t phy = mapped(PHY0_PHYS);
    mmio::write32(phy, PHY_KVCO_LEVEL, KVCO_LEVEL);
    mmio::write32(phy, PHY_KVCO_TUNE, KVCO_TUNE);
    update(syscon, SYSCON_RP_NEP, 0, RP_NEP_ROOT_PORT);
    update(syscon, SYSCON_AW, AW_CKREF_MASK, AW_CKREF_INTERNAL | AW_CLKREQ);

    update(mapped(SYSCRG_PHYS), SYS_CLK_NOC_STG_AXI, 0, CLOCK_ENABLE);
    for (uintptr_t clock : STG_CLOCKS)
        update(stg, clock, 0, CLOCK_ENABLE);
    update(stg, STG_RESET_ASSERT, STG_RESETS_PCIE0, 0);
    const uint64_t reset_end = now_us() + RESET_TIMEOUT_US;
    while ((mmio::read32(stg, STG_RESET_STATUS) & STG_RESETS_PCIE0) != STG_RESETS_PCIE0)
        ENSURE_LOG(now_us() < reset_end, Error::Timeout, "pcie: controller resets stuck");

    set_perst(false);
    disable_extra_functions();
    update(bridge, BRIDGE_GEN_SETTINGS, 0, GEN_ROOT_PORT);
    mmio::write32(bridge, BRIDGE_ROOT_BAR0, 0);
    mmio::write32(bridge, BRIDGE_ROOT_BAR1, 0);
    update(bridge, BRIDGE_CLASS, ~0xFFU, CLASS_PCI_BRIDGE);
    update(bridge, BRIDGE_LTR, LTR_FORWARD, 0);  // Forwarding LTR messages hangs the system
    update(bridge, BRIDGE_PREFETCH, 0, PREFETCH_64BIT);

    delay_us(PERST_HOLD_US);
    set_perst(true);
    delay_us(PERST_HOLD_US);
    const uint64_t link_end = now_us() + LINK_TIMEOUT_US;
    while ((mmio::read32(syscon, SYSCON_LINK_STATUS) & LINK_DATA_ACTIVE) == 0) {
        if (now_us() >= link_end) {
            klog::warn("pcie: no link on PCIe0\n");
            return Error::NoDevice;
        }
        delay_us(1000);
    }

    program_window(0, CONFIG_PHYS, 0, CONFIG_WINDOW_BYTES, true);
    program_window(1, MEMORY_PHYS, MEMORY_PHYS, MEMORY_WINDOW_BYTES, false);
    update(bridge, BRIDGE_INBOUND, 0, INBOUND_PASS_ALL);
    mmio::write32(bridge, BRIDGE_INBOUND + 4, 0);
    delay_us(LINK_SETTLE_US);
    klog::info("pcie: PCIe0 link up\n");
    return Error::None;
}

Error assign_resources() {
    // Root port register offsets in its type 1 header.
    constexpr int bridge_buses = 0x18;
    constexpr int bridge_io = 0x1C;
    constexpr int bridge_memory = 0x20;
    constexpr int bridge_prefetch = 0x24;
    constexpr int bus = 1;
    // The root port forwards configuration requests to bus 1 only once it
    // knows its secondary and subordinate bus numbers.
    pci::config_write32(0, 0, 0, bridge_buses, (1U << 16) | (1U << 8));
    const uint32_t id = pci::config_read32(bus, 0, 0, pci::VendorId);
    ENSURE_LOG(id != 0xFFFFFFFF && (id & 0xFFFF) != 0xFFFF && id != 0, Error::NoDevice,
               "pcie: no device behind the root port (0x%x)", id);

    uint64_t next = MEMORY_PHYS;
    for (int index = 0; index < 6; ++index) {
        const int offset = pci::Bar0 + index * 4;
        const uint32_t original = pci::config_read32(bus, 0, 0, offset);
        if ((original & 1U) != 0)
            continue;  // I/O space is not routed
        const bool wide = (original & 6U) == 4U;
        pci::config_write32(bus, 0, 0, offset, 0xFFFFFFFF);
        const uint32_t mask = pci::config_read32(bus, 0, 0, offset) & ~0xFU;
        if (mask == 0) {
            if (wide)
                ++index;
            continue;
        }
        const uint64_t size = static_cast<uint64_t>(~mask) + 1;
        next = (next + size - 1) & ~(size - 1);
        ENSURE_LOG(next + size <= MEMORY_PHYS + MEMORY_WINDOW_BYTES, Error::NoMem, "pcie: BAR %d does not fit", index);
        pci::config_write32(bus, 0, 0, offset, static_cast<uint32_t>(next));
        if (wide) {
            pci::config_write32(bus, 0, 0, offset + 4, static_cast<uint32_t>(next >> 32));
            ++index;
        }
        klog::info("pcie: %04x:%04x BAR%d at 0x%lx, %lu KiB\n", id & 0xFFFF, id >> 16, wide ? index - 1 : index, next,
                   size / 1024);
        next += size;
    }
    // A memory window over the assigned BARs, no I/O or prefetchable window,
    // then decode and mastering.
    const uint32_t limit = static_cast<uint32_t>((next == MEMORY_PHYS ? MEMORY_PHYS + 0xFFFFF : next - 1) >> 16);
    pci::config_write32(0, 0, 0, bridge_io, 0xF0);
    pci::config_write32(0, 0, 0, bridge_memory, ((limit & 0xFFF0) << 16) | ((MEMORY_PHYS >> 16) & 0xFFF0));
    pci::config_write32(0, 0, 0, bridge_prefetch, 0x0000FFF0);
    const uint32_t command = pci::config_read32(0, 0, 0, pci::Command);
    pci::config_write32(0, 0, 0, pci::Command, command | pci::CMD_MEMORY_SPACE | pci::CMD_BUS_MASTER);
    return Error::None;
}

bool present(int bus, int slot, int function) {
    return bus < BUSES && slot == 0 && function == 0;
}

bool hidden(int bus, int offset) {
    return bus == 0 && (offset == pci::Bar0 || offset == pci::Bar1);
}

}  // namespace jh7110_pcie
