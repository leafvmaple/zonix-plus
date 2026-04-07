#pragma once

#include <base/types.h>
#include "block/blk.h"
#include "lib/array.h"
#include "lib/result.h"

namespace pci {
struct DeviceInfo;
struct DriverId;
}  // namespace pci

class SdDevice : public BlockDevice {
public:
    Error init(volatile uint8_t* base, int index);
    Error read(uint32_t block_number, void* buf, size_t block_count) override;
    Error write(uint32_t block_number, const void* buf, size_t block_count) override;
    void print_info() override;

private:
    volatile uint8_t* base_{};
    uint16_t rca_{};
    bool sdhc_{};

    Error reset();
    Error clock_setup();
    Error power_on();
    Error send_cmd(uint8_t index, uint32_t arg, uint16_t flags);
    Error wait_cmd_done();
    Error wait_xfer_done();
    uint32_t read_response(int idx);

    Error card_identify();
    Error read_csd();
    Error read_single(uint32_t lba, void* buf);
    Error write_single(uint32_t lba, const void* buf);
};

namespace sdhci {

class Manager {
public:
    static constexpr int MAX_DEVICES = 4;

    static int init();
    static int device_count();
    static SdDevice* get_device(int index);

    static Error probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*);

private:
    inline static bool s_initialized{};
    inline static Array<SdDevice, MAX_DEVICES> s_devices{};
};

int init();
int device_count();
SdDevice* get_device();
SdDevice* get_device(int index);

}  // namespace sdhci
