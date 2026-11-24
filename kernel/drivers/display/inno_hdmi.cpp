#include "drivers/display/inno_hdmi.h"
#include "drivers/mmio.h"

namespace {

// Controller registers occupy indices 0x00-0xFF and the PHY 0x1A0-0x1D3.
namespace reg {
constexpr uint32_t SYS_CTRL = 0x00;
constexpr uint32_t VIDEO_TIMING_CTL = 0x08;
constexpr uint32_t VIDEO_HTOTAL_L = 0x09;
constexpr uint32_t VIDEO_HBLANK_L = 0x0B;
constexpr uint32_t VIDEO_HDELAY_L = 0x0D;
constexpr uint32_t VIDEO_HDURATION_L = 0x0F;
constexpr uint32_t VIDEO_VTOTAL_L = 0x11;
constexpr uint32_t VIDEO_VBLANK = 0x13;
constexpr uint32_t VIDEO_VDELAY = 0x14;
constexpr uint32_t VIDEO_VDURATION = 0x15;
constexpr uint32_t CONTROL_PACKET_BUF_INDEX = 0x9F;
constexpr uint32_t CONTROL_PACKET_ADDR = 0xA0;
constexpr uint32_t HDCP_CTRL = 0x52;
constexpr uint32_t COLORBAR = 0xC9;
constexpr uint32_t PHY_SYNC = 0xCE;
constexpr uint32_t PRE_PLL_CONTROL = 0x1A0;
constexpr uint32_t PRE_PLL_DIV_1 = 0x1A1;
constexpr uint32_t PRE_PLL_DIV_2 = 0x1A2;
constexpr uint32_t PRE_PLL_FB_DIV = 0x1A3;
constexpr uint32_t PRE_PLL_TMDS_DIV = 0x1A4;
constexpr uint32_t PRE_PLL_PCLK_DIV_B = 0x1A5;
constexpr uint32_t PRE_PLL_PCLK_DIV_CD = 0x1A6;
constexpr uint32_t PRE_PLL_LOCK_STATUS = 0x1A9;
constexpr uint32_t POST_PLL_DIV_1 = 0x1AA;
constexpr uint32_t POST_PLL_DIV_2 = 0x1AB;
constexpr uint32_t POST_PLL_FB_DIV = 0x1AC;
constexpr uint32_t POST_PLL_DIV_3 = 0x1AD;
constexpr uint32_t POST_PLL_LOCK_STATUS = 0x1AF;
constexpr uint32_t BIAS_CONTROL = 0x1B0;
constexpr uint32_t TMDS_CONTROL = 0x1B2;
constexpr uint32_t LDO_CONTROL = 0x1B4;
constexpr uint32_t SERIALIZER_CONTROL = 0x1BE;
constexpr uint32_t CLOCK_DRIVE = 0x1BF;
constexpr uint32_t DATA_DRIVE = 0x1C0;
constexpr uint32_t PLL_TEST = 0x1CC;
}  // namespace reg

constexpr uint8_t SYS_POWER_OFF = 0x63;  // Analog and digital out of reset, power down
constexpr uint8_t SYS_POWER_ON = 0x61;   // Interrupt polarity high, powered
constexpr uint8_t TIMING_EXTERNAL = 1U << 0;
constexpr uint8_t TIMING_HSYNC_POSITIVE = 1U << 2;
constexpr uint8_t TIMING_VSYNC_POSITIVE = 1U << 3;
constexpr uint8_t COLORBAR_OFF = 0x10;
constexpr uint8_t PLL_POWER_DOWN = 0x01;
constexpr uint8_t PLL_LOCKED = 1U << 0;
constexpr uint8_t POST_PLL_POWER_DOWN = 0x0F;
constexpr uint8_t POST_PLL_RUN = 0x0E;
constexpr uint8_t BIAS_ENABLE = 0x04;
constexpr uint8_t LDO_ENABLE = 0x07;
constexpr uint8_t SERIALIZER_ENABLE = 0x71;
constexpr uint8_t TMDS_ENABLE = 0x8F;
constexpr uint8_t PLL_TEST_DEFAULT = 0x0F;
constexpr uint8_t AVI_PACKET = 0x06;
constexpr uint8_t HDMI_MODE = 1U << 1;  // Clear: DVI, which sends no InfoFrames

constexpr uint8_t AVI_TYPE = 0x82;
constexpr uint8_t AVI_VERSION = 0x02;
constexpr uint8_t AVI_HEADER_BYTES = 3;
constexpr uint8_t AVI_PAYLOAD_BYTES = 13;
constexpr uint8_t AVI_RGB = 0x00;                     // PB1 Y1:Y0
constexpr uint8_t AVI_ACTIVE_FORMAT_PRESENT = 0x10;   // PB1 A0
constexpr uint8_t AVI_UNDERSCAN = 0x02;               // PB1 S1:S0
constexpr uint8_t AVI_PICTURE_16_9 = 0x20;            // PB2 M1:M0
constexpr uint8_t AVI_ACTIVE_SAME_AS_PICTURE = 0x08;  // PB2 R3:R0
constexpr uint8_t AVI_IT_CONTENT = 0x80;              // PB3 ITC
constexpr uint8_t AVI_RGB_FULL_RANGE = 0x08;          // PB3 Q1:Q0

// Pre-PLL from the 24 MHz reference: divide by 1, multiply by 99 (VCO
// 2376 MHz), then the TMDS and pixel-clock dividers per mode. The post-PLL
// multiplies the TMDS clock by 20 for the serializer.
struct PllSetting {
    uint32_t pixel_khz;
    uint8_t tmds_div;
    uint8_t pclk_div_b;
    uint8_t pclk_div_cd;
};
constexpr PllSetting PLL_SETTINGS[] = {
    {148500, 0x15, 0x41, 0x42},
    {74250, 0x1A, 0x41, 0x64},
};
constexpr uint8_t PRE_PLL_PREDIV = 0x01;
constexpr uint8_t PRE_PLL_DIV_2_VALUE = 0xF0;
constexpr uint8_t PRE_PLL_FBDIV_99 = 0x63;
constexpr uint8_t POST_PLL_PREDIV = 0x01;
constexpr uint8_t POST_PLL_FBDIV_20 = 0x14;
constexpr uint8_t POST_PLL_POSTDIV = 0x01;

void write(uintptr_t base, uint32_t index, uint8_t value) {
    mmio::write32(base, index * sizeof(uint32_t), value);
}

uint8_t read(uintptr_t base, uint32_t index) {
    return static_cast<uint8_t>(mmio::read32(base, index * sizeof(uint32_t)));
}

void write16(uintptr_t base, uint32_t low_index, uint32_t value) {
    write(base, low_index, static_cast<uint8_t>(value));
    write(base, low_index + 1, static_cast<uint8_t>(value >> 8));
}

void write_timing(uintptr_t base, const DisplayTiming& t) {
    write(base, reg::VIDEO_TIMING_CTL,
          TIMING_EXTERNAL | (t.hsync_positive ? TIMING_HSYNC_POSITIVE : 0) |
              (t.vsync_positive ? TIMING_VSYNC_POSITIVE : 0));
    write16(base, reg::VIDEO_HTOTAL_L, t.htotal);
    write16(base, reg::VIDEO_HBLANK_L, t.htotal - t.hactive);
    write16(base, reg::VIDEO_HDELAY_L, t.htotal - t.hsync_start);
    write16(base, reg::VIDEO_HDURATION_L, t.hsync_end - t.hsync_start);
    write16(base, reg::VIDEO_VTOTAL_L, t.vtotal);
    write(base, reg::VIDEO_VBLANK, static_cast<uint8_t>(t.vtotal - t.vactive));
    write(base, reg::VIDEO_VDELAY, static_cast<uint8_t>(t.vtotal - t.vsync_start));
    write(base, reg::VIDEO_VDURATION, static_cast<uint8_t>(t.vsync_end - t.vsync_start));
}

// AVI InfoFrame (CEA-861 version 2): full-range RGB computer graphics,
// 16:9, underscanned, the mode's VIC. Televisions that honour it show the
// whole picture instead of cropping its edges as they do for video.
void write_avi_infoframe(uintptr_t base, const DisplayTiming& t) {
    uint8_t frame[AVI_HEADER_BYTES + 1 + AVI_PAYLOAD_BYTES] = {AVI_TYPE, AVI_VERSION, AVI_PAYLOAD_BYTES};
    uint8_t* payload = frame + AVI_HEADER_BYTES + 1;
    payload[0] = AVI_RGB | AVI_ACTIVE_FORMAT_PRESENT | AVI_UNDERSCAN;
    payload[1] = AVI_PICTURE_16_9 | AVI_ACTIVE_SAME_AS_PICTURE;
    payload[2] = AVI_IT_CONTENT | AVI_RGB_FULL_RANGE;
    payload[3] = t.vic;
    uint8_t sum = 0;
    for (uint8_t byte : frame)
        sum = static_cast<uint8_t>(sum + byte);
    frame[AVI_HEADER_BYTES] = static_cast<uint8_t>(-sum);  // All bytes sum to zero.
    write(base, reg::CONTROL_PACKET_BUF_INDEX, AVI_PACKET);
    for (uint32_t i = 0; i < sizeof(frame); ++i)
        write(base, reg::CONTROL_PACKET_ADDR + i, frame[i]);
}

}  // namespace

namespace inno_hdmi {

Error configure_plls(uintptr_t base, const DisplayTiming& timing) {
    const PllSetting* setting = nullptr;
    for (const auto& candidate : PLL_SETTINGS)
        if (candidate.pixel_khz == timing.pixel_khz)
            setting = &candidate;
    ENSURE(setting, Error::NotSupported);

    write(base, reg::BIAS_CONTROL, read(base, reg::BIAS_CONTROL) | BIAS_ENABLE);
    write(base, reg::PLL_TEST, PLL_TEST_DEFAULT);
    write(base, reg::PRE_PLL_CONTROL, PLL_POWER_DOWN);
    write(base, reg::POST_PLL_DIV_1, POST_PLL_POWER_DOWN);
    write(base, reg::PRE_PLL_DIV_1, PRE_PLL_PREDIV);
    write(base, reg::PRE_PLL_DIV_2, PRE_PLL_DIV_2_VALUE);
    write(base, reg::PRE_PLL_FB_DIV, PRE_PLL_FBDIV_99);
    write(base, reg::PRE_PLL_TMDS_DIV, setting->tmds_div);
    write(base, reg::PRE_PLL_PCLK_DIV_B, setting->pclk_div_b);
    write(base, reg::PRE_PLL_PCLK_DIV_CD, setting->pclk_div_cd);
    write(base, reg::POST_PLL_DIV_2, POST_PLL_PREDIV);
    write(base, reg::POST_PLL_FB_DIV, POST_PLL_FBDIV_20);
    write(base, reg::POST_PLL_DIV_3, POST_PLL_POSTDIV);
    write(base, reg::POST_PLL_DIV_1, POST_PLL_RUN);
    write(base, reg::PRE_PLL_CONTROL, 0);
    return Error::None;
}

bool plls_locked(uintptr_t base) {
    return (read(base, reg::PRE_PLL_LOCK_STATUS) & PLL_LOCKED) != 0 &&
           (read(base, reg::POST_PLL_LOCK_STATUS) & PLL_LOCKED) != 0;
}

void enable(uintptr_t base, const DisplayTiming& timing) {
    write(base, reg::LDO_CONTROL, LDO_ENABLE);
    write(base, reg::SERIALIZER_CONTROL, SERIALIZER_ENABLE);
    // Drive strength the vendor tuned for 1080p; lower rates use the default.
    const bool fast = timing.pixel_khz > 100000;
    write(base, reg::CLOCK_DRIVE, fast ? 0x02 : 0x00);
    write(base, reg::DATA_DRIVE, fast ? 0x22 : 0x00);
    write(base, reg::SYS_CTRL, SYS_POWER_OFF);
    write_timing(base, timing);
    write(base, reg::HDCP_CTRL, read(base, reg::HDCP_CTRL) | HDMI_MODE);
    write_avi_infoframe(base, timing);
    write(base, reg::COLORBAR, COLORBAR_OFF);
    write(base, reg::SYS_CTRL, SYS_POWER_ON);
    write(base, reg::TMDS_CONTROL, TMDS_ENABLE);
    write(base, reg::PHY_SYNC, 0);
    write(base, reg::PHY_SYNC, 1);
}

void disable(uintptr_t base) {
    write(base, reg::TMDS_CONTROL, 0);
    write(base, reg::SYS_CTRL, SYS_POWER_OFF);
    write(base, reg::PRE_PLL_CONTROL, PLL_POWER_DOWN);
    write(base, reg::POST_PLL_DIV_1, POST_PLL_POWER_DOWN);
}

}  // namespace inno_hdmi
