// JH7110 display bring-up: PMU power domain, system and VOUT clock/reset
// generators, then the Innosilicon HDMI transmitter and the DC8200 display
// controller at a fixed CEA mode. Firmware leaves all of it off.

#include "jh7110_display.h"
#include "timer.h"
#include "drivers/display/dc8200.h"
#include "drivers/display/inno_hdmi.h"
#include "drivers/mmio.h"
#include "lib/memory.h"
#include "mm/pmm.h"
#include <kernel/log.h>

#include <asm/arch.h>
#include <asm/board.h>
#include <asm/memlayout.h>
#include <asm/page.h>

namespace {

constexpr uintptr_t PMU_PHYS = 0x17030000;
constexpr uintptr_t SYSCRG_PHYS = 0x13020000;
constexpr uintptr_t VOUTCRG_PHYS = 0x295C0000;
constexpr uintptr_t DC8200_PHYS = 0x29400000;
constexpr uintptr_t HDMI_PHYS = 0x29590000;

// PMU software power sequencing.
constexpr uintptr_t PMU_SW_TURN_ON = 0x0C;
constexpr uintptr_t PMU_SW_ENCOURAGE = 0x44;
constexpr uintptr_t PMU_CURRENT_POWER = 0x80;
constexpr uint32_t PMU_DOMAIN_VOUT = 1U << 4;
constexpr uint32_t PMU_ENCOURAGE_RESET = 0xFF;
constexpr uint32_t PMU_ENCOURAGE_ON_1 = 0x05;
constexpr uint32_t PMU_ENCOURAGE_ON_2 = 0x50;

// Clock generators: one register per clock, gate in bit 31, mux in 29:24,
// divider in 23:0. System resets assert at 0x2F8 and report released (1)
// at 0x308, one bit per reset across four words.
constexpr uint32_t CLOCK_ENABLE = 1U << 31;
constexpr uintptr_t SYS_CLK_VOUT_SRC = 0xE8;
constexpr uintptr_t SYS_CLK_NOC_DISP_AXI = 0xF0;
constexpr uintptr_t SYS_CLK_VOUT_TOP_AHB = 0xF4;
constexpr uintptr_t SYS_CLK_VOUT_TOP_AXI = 0xF8;
constexpr uintptr_t SYS_CLK_HDMITX0_MCLK = 0xFC;
constexpr uintptr_t SYS_RESET_ASSERT = 0x2F8;
constexpr uintptr_t SYS_RESET_STATUS = 0x308;
constexpr uint32_t SYS_RESET_NOC_DISP_AXI = 26;
constexpr uint32_t SYS_RESET_VOUT_TOP_SRC = 43;

constexpr uintptr_t VOUT_CLK_DC8200_PIX_DIV = 0x04;
constexpr uintptr_t VOUT_CLK_DC8200_AXI = 0x10;
constexpr uintptr_t VOUT_CLK_DC8200_CORE = 0x14;
constexpr uintptr_t VOUT_CLK_DC8200_AHB = 0x18;
constexpr uintptr_t VOUT_CLK_DC8200_PIX0 = 0x1C;  // Mux 0: the pixel divider
constexpr uintptr_t VOUT_CLK_DC8200_PIX1 = 0x20;
constexpr uintptr_t VOUT_CLK_HDMI_MCLK = 0x3C;
constexpr uintptr_t VOUT_CLK_HDMI_BCLK = 0x40;
constexpr uintptr_t VOUT_CLK_HDMI_SYS = 0x44;
constexpr uintptr_t VOUT_RESET_ASSERT = 0x48;
constexpr uintptr_t VOUT_RESET_STATUS = 0x4C;
constexpr uint32_t VOUT_RESET_DC8200 = 0x7;  // AXI, AHB, core
constexpr uint32_t VOUT_RESET_HDMI = 1U << 9;
constexpr uint32_t VOUT_RESETS = VOUT_RESET_DC8200 | VOUT_RESET_HDMI;
constexpr uintptr_t VOUT_GATES[] = {VOUT_CLK_DC8200_AXI,  VOUT_CLK_DC8200_CORE, VOUT_CLK_DC8200_AHB,
                                    VOUT_CLK_DC8200_PIX0, VOUT_CLK_DC8200_PIX1, VOUT_CLK_HDMI_MCLK,
                                    VOUT_CLK_HDMI_BCLK,   VOUT_CLK_HDMI_SYS};

// PLL2 runs at 1188 MHz and feeds vout_src; dividing by 8 gives exactly the
// 148.5 MHz of 1080p60, from the same crystal as the HDMI PHY.
constexpr uint32_t VOUT_SOURCE_KHZ = 1188000;

constexpr uint64_t POWER_TIMEOUT_US = 100000;
constexpr uint64_t RESET_TIMEOUT_US = 10000;
constexpr uint64_t PLL_TIMEOUT_US = 100000;
constexpr uint64_t POWER_SETTLE_US = 50000;
constexpr uint64_t PHY_SETTLE_US = 20000;
constexpr uint64_t OUTPUT_SETTLE_US = 50000;

constexpr DisplayTiming MODE = CEA_1080P60;

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

template<typename Ready>
bool wait_until(uint64_t timeout_us, Ready&& ready) {
    const uint64_t end = now_us() + timeout_us;
    while (!ready()) {
        if (now_us() >= end)
            return ready();
        arch_spin_hint();
    }
    return true;
}

void set_bits(uintptr_t base, uintptr_t offset, uint32_t bits) {
    mmio::write32(base, offset, mmio::read32(base, offset) | bits);
}

void clear_bits(uintptr_t base, uintptr_t offset, uint32_t bits) {
    mmio::write32(base, offset, mmio::read32(base, offset) & ~bits);
}

Error power_on_vout() {
    const uintptr_t pmu = mapped(PMU_PHYS);
    if ((mmio::read32(pmu, PMU_CURRENT_POWER) & PMU_DOMAIN_VOUT) != 0)
        return Error::None;
    mmio::write32(pmu, PMU_SW_TURN_ON, PMU_DOMAIN_VOUT);
    mmio::write32(pmu, PMU_SW_ENCOURAGE, PMU_ENCOURAGE_RESET);
    mmio::write32(pmu, PMU_SW_ENCOURAGE, PMU_ENCOURAGE_ON_1);
    mmio::write32(pmu, PMU_SW_ENCOURAGE, PMU_ENCOURAGE_ON_2);
    ENSURE_LOG(
        wait_until(POWER_TIMEOUT_US, [pmu] { return (mmio::read32(pmu, PMU_CURRENT_POWER) & PMU_DOMAIN_VOUT) != 0; }),
        Error::Timeout, "display: VOUT power domain did not come up");
    delay_us(POWER_SETTLE_US);
    return Error::None;
}

Error release_system_reset(uint32_t id) {
    const uintptr_t crg = mapped(SYSCRG_PHYS);
    const uintptr_t word = id / 32 * sizeof(uint32_t);
    const uint32_t bit = 1U << (id % 32);
    clear_bits(crg, SYS_RESET_ASSERT + word, bit);
    ENSURE_LOG(wait_until(RESET_TIMEOUT_US,
                          [crg, word, bit] { return (mmio::read32(crg, SYS_RESET_STATUS + word) & bit) != 0; }),
               Error::Timeout, "display: system reset %u stuck", id);
    return Error::None;
}

Error start_vout_clocks() {
    const uintptr_t sys = mapped(SYSCRG_PHYS);
    set_bits(sys, SYS_CLK_NOC_DISP_AXI, CLOCK_ENABLE);
    TRY(release_system_reset(SYS_RESET_NOC_DISP_AXI));
    set_bits(sys, SYS_CLK_VOUT_SRC, CLOCK_ENABLE);
    set_bits(sys, SYS_CLK_VOUT_TOP_AXI, CLOCK_ENABLE);
    set_bits(sys, SYS_CLK_VOUT_TOP_AHB, CLOCK_ENABLE);
    set_bits(sys, SYS_CLK_HDMITX0_MCLK, CLOCK_ENABLE);
    TRY(release_system_reset(SYS_RESET_VOUT_TOP_SRC));

    const uintptr_t vout = mapped(VOUTCRG_PHYS);
    mmio::write32(vout, VOUT_CLK_DC8200_PIX_DIV, VOUT_SOURCE_KHZ / MODE.pixel_khz);
    for (uintptr_t gate : VOUT_GATES)
        mmio::write32(vout, gate, CLOCK_ENABLE);
    clear_bits(vout, VOUT_RESET_ASSERT, VOUT_RESETS);
    ENSURE_LOG(wait_until(RESET_TIMEOUT_US,
                          [vout] { return (mmio::read32(vout, VOUT_RESET_STATUS) & VOUT_RESETS) == VOUT_RESETS; }),
               Error::Timeout, "display: VOUT resets stuck (status 0x%x)", mmio::read32(vout, VOUT_RESET_STATUS));
    return Error::None;
}

Error start_hdmi() {
    const uintptr_t hdmi = mapped(HDMI_PHYS);
    TRY(inno_hdmi::configure_plls(hdmi, MODE));
    ENSURE_LOG(wait_until(PLL_TIMEOUT_US, [hdmi] { return inno_hdmi::plls_locked(hdmi); }), Error::Timeout,
               "display: HDMI PHY PLLs did not lock");
    delay_us(PHY_SETTLE_US);
    inno_hdmi::enable(hdmi, MODE);
    delay_us(OUTPUT_SETTLE_US);
    return Error::None;
}

class Framebuffer {
public:
    // Physically contiguous RAM below 4 GiB, which the controller addresses.
    static Error allocate(uint32_t stride, uint32_t height) {
        const size_t bytes = static_cast<size_t>(stride) * height;
        const size_t pages = (bytes + PG_SIZE - 1) / PG_SIZE;
        Page* page = pmm::alloc_pages(pages);
        ENSURE_LOG(page, Error::NoMem, "display: no %lu contiguous pages for the framebuffer", pages);
        const uintptr_t pa = pmm::page_to_phys(page);
        if (pa + bytes > 0x100000000ULL) {
            pmm::free_pages(page, pages);
            return Error::NoMem;
        }
        memset(pmm::page_to_kva(page), 0, bytes);
        display::flush_platform(pa, bytes);
        pa_ = pa;
        return Error::None;
    }
    static uintptr_t pa() { return pa_; }

private:
    inline static uintptr_t pa_{};
};

// The image the cursor plane reads: one page of contiguous RAM below 4 GiB,
// allocated when a pointer is first shown and kept for later ones.
class CursorBuffer {
public:
    static constexpr size_t BYTES = dc8200::CURSOR_SIZE * dc8200::CURSOR_SIZE * sizeof(uint32_t);
    static_assert(BYTES <= PG_SIZE);

    static Error load(const uint32_t* pixels) {
        if (!page_) {
            Page* page = pmm::alloc_pages(1);
            ENSURE(page, Error::NoMem);
            if (pmm::page_to_phys(page) + PG_SIZE > 0x100000000ULL) {
                pmm::free_pages(page, 1);
                return Error::NoMem;
            }
            page_ = page;
        }
        memcpy(pmm::page_to_kva(page_), pixels, BYTES);
        display::flush_platform(pmm::page_to_phys(page_), BYTES);
        return Error::None;
    }
    static uint32_t pa() { return static_cast<uint32_t>(pmm::page_to_phys(page_)); }

private:
    inline static Page* page_{};
};

}  // namespace

namespace jh7110_display {

static_assert(DISPLAY_CURSOR_SIZE == dc8200::CURSOR_SIZE && DISPLAY_CURSOR_SIZE - 1 <= dc8200::HOT_LIMIT);

Error show_cursor(const display::CursorImage& image, uint32_t x, uint32_t y) {
    TRY(CursorBuffer::load(image.pixels));
    dc8200::show_cursor(mapped(DC8200_PHYS), CursorBuffer::pa(), image.hot_x, image.hot_y, x, y);
    return Error::None;
}

void move_cursor(uint32_t x, uint32_t y) {
    dc8200::move_cursor(mapped(DC8200_PHYS), x, y);
}

void hide_cursor() {
    dc8200::hide_cursor(mapped(DC8200_PHYS));
}

Error start(display::PlatformFramebuffer& out) {
    const uint32_t stride = MODE.hactive * sizeof(uint32_t);
    TRY(power_on_vout());
    TRY(start_vout_clocks());
    TRY(start_hdmi());
    TRY(Framebuffer::allocate(stride, MODE.vactive));
    dc8200::start(mapped(DC8200_PHYS), MODE, static_cast<uint32_t>(Framebuffer::pa()), stride);
    // The hotplug pin is not routed to the transmitter, so its status means nothing.
    klog::info("display: HDMI %ux%u@60\n", MODE.hactive, MODE.vactive);
    out = {Framebuffer::pa(), MODE.hactive, MODE.vactive, stride, FramebufferFormat::Xrgb8888, false};
    return Error::None;
}

}  // namespace jh7110_display
