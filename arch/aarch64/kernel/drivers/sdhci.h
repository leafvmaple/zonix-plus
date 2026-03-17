// SDHCI (SD Host Controller Interface) driver

#pragma once

#include <base/types.h>
#include "block/blk.h"

namespace pci {
struct DeviceInfo;
struct DriverId;
}  // namespace pci

class SdDevice : public BlockDevice {
public:
    int init(volatile uint8_t* base, int index);
    int read(uint32_t block_number, void* buf, size_t block_count) override;
    int write(uint32_t block_number, const void* buf, size_t block_count) override;
    void print_info() override;
    void test() override;

private:
    volatile uint8_t* base_{};
    uint16_t rca_{};
    bool sdhc_{};

    uint32_t read32(uint32_t off);
    void write32(uint32_t off, uint32_t val);
    uint16_t read16(uint32_t off);
    void write16(uint32_t off, uint16_t val);
    uint8_t read8(uint32_t off);
    void write8(uint32_t off, uint8_t val);

    int reset();
    int clock_setup();
    int power_on();
    int send_cmd(uint8_t index, uint32_t arg, uint16_t flags);
    int wait_cmd_done();
    int wait_xfer_done();
    uint32_t read_response(int idx);

    int card_identify();
    int read_single(uint32_t lba, void* buf);
    int write_single(uint32_t lba, const void* buf);
};

namespace sdhci {

class Manager {
public:
    static constexpr int MAX_DEVICES = 4;

    static int init();
    static int device_count();
    static SdDevice* get_device(int index);

    static int probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*);

private:
    inline static bool s_initialized{};
    inline static int s_device_count{};
    inline static SdDevice s_devices[MAX_DEVICES]{};
};

int init();
int device_count();
SdDevice* get_device();
SdDevice* get_device(int index);

}  // namespace sdhci
