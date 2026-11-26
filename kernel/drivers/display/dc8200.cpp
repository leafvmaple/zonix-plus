#include "drivers/display/dc8200.h"
#include "drivers/mmio.h"

namespace {

// Display 0, primary plane. Display 1 and its plane add 4 to each offset.
namespace reg {
constexpr uintptr_t FRAMEBUFFER_ADDRESS = 0x1400;
constexpr uintptr_t FRAMEBUFFER_STRIDE = 0x1408;
constexpr uintptr_t DITHER_CONFIG = 0x1410;
constexpr uintptr_t PANEL_CONFIG = 0x1418;
constexpr uintptr_t DISPLAY_H = 0x1430;
constexpr uintptr_t DISPLAY_H_SYNC = 0x1438;
constexpr uintptr_t DISPLAY_V = 0x1440;
constexpr uintptr_t DISPLAY_V_SYNC = 0x1448;
constexpr uintptr_t CURSOR_CONFIG = 0x1468;
constexpr uintptr_t CURSOR_ADDRESS = 0x146C;
constexpr uintptr_t CURSOR_LOCATION = 0x1470;
constexpr uintptr_t VENDOR_14B0 = 0x14B0;  // Undocumented; the vendor boot loader writes these
constexpr uintptr_t VENDOR_14D0 = 0x14D0;
constexpr uintptr_t VENDOR_1800 = 0x1800;
constexpr uintptr_t DPI_CONFIG = 0x14B8;
constexpr uintptr_t FRAMEBUFFER_CONFIG = 0x1518;
constexpr uintptr_t BACKGROUND_COLOR = 0x1528;
constexpr uintptr_t FRAMEBUFFER_SIZE = 0x1810;
constexpr uintptr_t CLOCK_GATING = 0x1A28;
constexpr uintptr_t FRAMEBUFFER_CONFIG_EX = 0x1CC0;
constexpr uintptr_t PANEL_START = 0x1CCC;
constexpr uintptr_t DP_CONFIG = 0x1CD0;
constexpr uintptr_t FRAMEBUFFER_WATER_MARK = 0x1CE8;
constexpr uintptr_t FRAMEBUFFER_TOP_LEFT = 0x24D8;
constexpr uintptr_t FRAMEBUFFER_BOTTOM_RIGHT = 0x24E0;
constexpr uintptr_t FRAMEBUFFER_SRC_GLOBAL_COLOR = 0x2500;
constexpr uintptr_t FRAMEBUFFER_DST_GLOBAL_COLOR = 0x2508;
constexpr uintptr_t FRAMEBUFFER_BLEND_CONFIG = 0x2510;
constexpr uintptr_t PANEL_CONFIG_EX = 0x2518;
}  // namespace reg

constexpr uint32_t FORMAT_X8R8G8B8 = 5U << 26;
constexpr uint32_t LAYER_ENABLE = 1U << 13;
constexpr uint32_t LAYER_SHADOW = 1U << 12;
constexpr uint32_t SYNC_ENABLE = 1U << 30;
constexpr uint32_t SYNC_NEGATIVE = 1U << 31;
constexpr uint32_t PANEL_HOLD_SHADOW = 1U << 0;
constexpr uint32_t PANEL_OUTPUT_ENABLE = 1U << 12;
constexpr uint32_t PANEL_DEFAULTS = 0x111;  // Data, DE and clock enabled, active high
constexpr uint32_t DPI_RGB888 = 5;
constexpr uint32_t DP_CONFIG_RGB888 = 0xD2;  // As the vendor boot loader drives HDMI
constexpr uint32_t BLEND_OPAQUE = 0x3548;    // Source over background, no per-pixel alpha
constexpr uint32_t WATER_MARK = 0xF61;
constexpr uint32_t START_DISPLAY0 = 1U << 0;
constexpr uint32_t STOP_ALL_CLOCKS = 0xE07;
// Cursor configuration: format in bits 1:0, an image-update and a commit bit
// that latch the other cursor registers, the size code in bits 7:5 (0 for
// 32x32) and the hot pixel's row and column in bits 12:8 and 20:16.
constexpr uint32_t CURSOR_FORMAT_ARGB8888 = 2;
constexpr uint32_t CURSOR_IMAGE_UPDATE = 1U << 2;
constexpr uint32_t CURSOR_COMMIT = 1U << 3;
constexpr uint32_t CURSOR_SIZE_32 = 0U << 5;
constexpr uint32_t CURSOR_HOT_Y_SHIFT = 8;
constexpr uint32_t CURSOR_HOT_X_SHIFT = 16;
constexpr uint32_t CURSOR_HOT_MASK = 0x1F;
// The cursor location holds the column in bits 14:0 and the row in bits 30:16.
constexpr uint32_t CURSOR_COORDINATE_MASK = 0x7FFF;
constexpr uint32_t CURSOR_ROW_SHIFT = 16;

uint32_t point(uint32_t x, uint32_t y) {
    return x | (y << 15);
}

uint32_t cursor_location(uint32_t x, uint32_t y) {
    return (x & CURSOR_COORDINATE_MASK) | ((y & CURSOR_COORDINATE_MASK) << CURSOR_ROW_SHIFT);
}

void commit_cursor(uintptr_t base, uint32_t bits) {
    mmio::write32(base, reg::CURSOR_CONFIG, mmio::read32(base, reg::CURSOR_CONFIG) | bits);
}

uint32_t sync(uint16_t start, uint16_t end, bool positive) {
    return start | (static_cast<uint32_t>(end) << 15) | SYNC_ENABLE | (positive ? 0 : SYNC_NEGATIVE);
}

}  // namespace

namespace dc8200 {

void start(uintptr_t base, const DisplayTiming& timing, uint32_t framebuffer_pa, uint32_t stride) {
    // Shadowed registers take effect together once the hold is released.
    mmio::write32(base, reg::PANEL_CONFIG_EX, PANEL_HOLD_SHADOW);
    mmio::write32(base, reg::CLOCK_GATING, 0);
    mmio::write32(base, reg::FRAMEBUFFER_CONFIG_EX, LAYER_ENABLE);
    mmio::write32(base, reg::VENDOR_1800, 0);
    mmio::write32(base, reg::FRAMEBUFFER_TOP_LEFT, point(0, 0));
    mmio::write32(base, reg::FRAMEBUFFER_BOTTOM_RIGHT, point(timing.hactive, timing.vactive));
    mmio::write32(base, reg::FRAMEBUFFER_SIZE, point(timing.hactive, timing.vactive));
    mmio::write32(base, reg::FRAMEBUFFER_ADDRESS, framebuffer_pa);
    mmio::write32(base, reg::FRAMEBUFFER_STRIDE, stride);
    mmio::write32(base, reg::FRAMEBUFFER_WATER_MARK, WATER_MARK);
    mmio::write32(base, reg::FRAMEBUFFER_BLEND_CONFIG, BLEND_OPAQUE);
    mmio::write32(base, reg::FRAMEBUFFER_SRC_GLOBAL_COLOR, 0xFF000000);
    mmio::write32(base, reg::FRAMEBUFFER_DST_GLOBAL_COLOR, 0);
    mmio::write32(base, reg::FRAMEBUFFER_CONFIG, FORMAT_X8R8G8B8);
    mmio::write32(base, reg::FRAMEBUFFER_CONFIG_EX, LAYER_ENABLE | LAYER_SHADOW);

    mmio::write32(base, reg::DISPLAY_H, timing.hactive | (static_cast<uint32_t>(timing.htotal) << 16));
    mmio::write32(base, reg::DISPLAY_H_SYNC, sync(timing.hsync_start, timing.hsync_end, timing.hsync_positive));
    mmio::write32(base, reg::DISPLAY_V, timing.vactive | (static_cast<uint32_t>(timing.vtotal) << 16));
    mmio::write32(base, reg::DISPLAY_V_SYNC, sync(timing.vsync_start, timing.vsync_end, timing.vsync_positive));
    mmio::write32(base, reg::VENDOR_14B0, 0);
    mmio::write32(base, reg::DP_CONFIG, DP_CONFIG_RGB888);
    mmio::write32(base, reg::DPI_CONFIG, DPI_RGB888);
    mmio::write32(base, reg::VENDOR_14D0, 0x52);
    mmio::write32(base, reg::BACKGROUND_COLOR, 0);
    mmio::write32(base, reg::PANEL_CONFIG, PANEL_DEFAULTS | PANEL_OUTPUT_ENABLE);
    mmio::write32(base, reg::DITHER_CONFIG, 0);
    mmio::write32(base, reg::CURSOR_CONFIG, 0);
    mmio::write32(base, reg::PANEL_CONFIG_EX, 0);
    mmio::write32(base, reg::PANEL_START, START_DISPLAY0);
}

void show_cursor(uintptr_t base, uint32_t image_pa, uint32_t hot_x, uint32_t hot_y, uint32_t x, uint32_t y) {
    mmio::write32(base, reg::CURSOR_ADDRESS, image_pa);
    mmio::write32(base, reg::CURSOR_LOCATION, cursor_location(x, y));
    mmio::write32(base, reg::CURSOR_CONFIG,
                  CURSOR_FORMAT_ARGB8888 | CURSOR_SIZE_32 | ((hot_y & CURSOR_HOT_MASK) << CURSOR_HOT_Y_SHIFT) |
                      ((hot_x & CURSOR_HOT_MASK) << CURSOR_HOT_X_SHIFT));
    commit_cursor(base, CURSOR_COMMIT | CURSOR_IMAGE_UPDATE);
}

void move_cursor(uintptr_t base, uint32_t x, uint32_t y) {
    mmio::write32(base, reg::CURSOR_LOCATION, cursor_location(x, y));
    commit_cursor(base, CURSOR_COMMIT);
}

// The plane has no enable bit; with no format it shows nothing.
void hide_cursor(uintptr_t base) {
    mmio::write32(base, reg::CURSOR_CONFIG, 0);
    commit_cursor(base, CURSOR_COMMIT);
}

void stop(uintptr_t base) {
    mmio::write32(base, reg::PANEL_START, 0);
    mmio::write32(base, reg::PANEL_CONFIG, PANEL_DEFAULTS);
    mmio::write32(base, reg::FRAMEBUFFER_CONFIG_EX, 0);
    mmio::write32(base, reg::CLOCK_GATING, STOP_ALL_CLOCKS);
}

}  // namespace dc8200
