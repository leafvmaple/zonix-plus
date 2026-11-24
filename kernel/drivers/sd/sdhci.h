#pragma once

// SD Host Controller Standard (SDHCI) host, found behind PCI on the QEMU
// machines. The card protocol is SdCard's; this file drives the registers.

#include <base/types.h>
#include "drivers/sd/sd_card.h"
#include "drivers/sd/sd_host.h"
#include <sys/array.hpp>
#include "lib/result.h"
#include "lib/mutex.h"
#include "mm/mmio.h"

namespace pci {
struct DeviceInfo;
struct DriverId;
}  // namespace pci

class SdhciHost final : public SdHost {
public:
    void attach(volatile uint8_t* base) { base_ = base; }

    Error start() override;
    void stop() override;
    Error execute(SdCommand& command) override;

private:
    Error reset();
    Error clock_setup();
    Error power_on();
    Error send_cmd(uint8_t index, uint32_t arg, uint16_t flags);
    Error wait_cmd_done();
    Error wait_xfer_done();
    Error wait_buffer(uint16_t ready);
    uint32_t read_response(int index);

    volatile uint8_t* base_{};
};

// An SD card on an SDHCI controller.
class SdhciDevice final : public SdCard {
public:
    SdhciDevice() : SdCard(host_) {}

    Error init(volatile uint8_t* base, int index);

private:
    SdhciHost host_{};
};

namespace sdhci {

class Manager {
public:
    static constexpr int MAX_DEVICES = 4;

    static int init();
    static int device_count();
    static SdhciDevice* find_device(int index);

    static Error probe_callback(const pci::DeviceInfo* pdev, const pci::DriverId*);

private:
    struct ControllerLocation {
        uint8_t bus;
        uint8_t slot;
        uint8_t function;
    };
    inline static Mutex probe_mutex_{};
    inline static bool initialized_{};
    inline static sys::array<SdhciDevice, MAX_DEVICES> devices_{};
    inline static sys::array<vmm::MmioRegion, MAX_DEVICES> mappings_{};
    inline static sys::array<ControllerLocation, MAX_DEVICES> locations_{};
    inline static size_t device_count_{};
};

int init();
int device_count();
SdhciDevice* find_device();
SdhciDevice* find_device(int index);

}  // namespace sdhci
