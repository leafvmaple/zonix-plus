#pragma once

#include <base/types.h>
#include "block/blk.h"
#include <sys/array.hpp>
#include "lib/result.h"
#include "lib/mutex.h"
#include "mm/mmio.h"

namespace pci {
struct DeviceInfo;
struct DriverId;
}  // namespace pci

class SdDevice : public BlockDevice {
public:
    Error init(volatile uint8_t* base, int index);
    void shutdown();
    [[nodiscard]] blk::DeviceState state() const { return state_; }
    Error read(uint32_t start_lba, void* buf, size_t block_count) override;
    Error write(uint32_t start_lba, const void* buf, size_t block_count) override;
    void print_info() override;

private:
    Mutex io_mutex_{};
    blk::DeviceState state_{blk::DeviceState::Offline};
    volatile uint8_t* base_{};
    uint16_t rca_{};
    bool sdhc_{};

    Error reset();
    Error clock_setup();
    Error power_on();
    Error send_cmd(uint8_t index, uint32_t arg, uint16_t flags);
    Error wait_cmd_done();
    Error wait_xfer_done();
    uint32_t read_response(int index);

    Error card_identify();
    Error read_csd();
    Error read_single(uint32_t lba, void* buf);
    Error write_single(uint32_t lba, const void* buf);
    void shutdown_locked();
    Error transfer_blocks(uint32_t start_lba, void* buf, size_t count, bool write);
};

namespace sdhci {

class Manager {
public:
    static constexpr int MAX_DEVICES = 4;

    static int init();
    static int device_count();
    static SdDevice* find_device(int index);

    static Error probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*);

private:
    struct ControllerLocation {
        uint8_t bus;
        uint8_t slot;
        uint8_t function;
    };
    inline static Mutex probe_mutex_{};
    inline static bool initialized_{};
    inline static sys::array<SdDevice, MAX_DEVICES> devices_{};
    inline static sys::array<vmm::MmioRegion, MAX_DEVICES> mappings_{};
    inline static sys::array<ControllerLocation, MAX_DEVICES> locations_{};
    inline static size_t device_count_{};
};

int init();
int device_count();
SdDevice* find_device();
SdDevice* find_device(int index);

}  // namespace sdhci
