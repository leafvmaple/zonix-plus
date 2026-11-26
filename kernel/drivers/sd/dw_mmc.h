#pragma once

// Synopsys DesignWare Mobile Storage Host (DW MSHC) controller.
//
// Platform code supplies the register address and the rate of the clock
// feeding the card clock divider. Firmware is expected to have enabled the
// controller's clocks, resets and pins. Data moves by polled PIO through the
// FIFO; the internal DMA engine stays off.

#include <base/types.h>
#include "drivers/sd/sd_card.h"
#include "drivers/sd/sd_host.h"
#include "lib/result.h"
#include "mm/mmio.h"

class DwMmcHost final : public SdHost {
public:
    void attach(volatile uint8_t* base, uint64_t clock_hz) {
        base_ = base;
        clock_hz_ = clock_hz;
    }

    Error start() override;
    void stop() override;
    Error execute(SdCommand& command) override;
    bool supports_wide_bus() const override { return true; }
    Error set_wide_bus() override;
    Error set_transfer_clock(uint32_t rate_hz) override;

private:
    Error reset();
    Error update_clock();
    Error set_clock(uint32_t rate_hz);
    Error wait_idle();
    Error wait_status(uint32_t mask, const char* what);
    Error issue(SdCommand& command, uint32_t flags);
    Error move_data(SdCommand& command);

    volatile uint8_t* base_{};
    uint64_t clock_hz_{};
    uint32_t data_offset_{};
};

// An SD card on a DesignWare controller.
class DwMmcDevice final : public SdCard {
public:
    DwMmcDevice() : SdCard(host_) {}

    Error init(volatile uint8_t* base, uint64_t clock_hz, int index);

private:
    DwMmcHost host_{};
};

namespace dw_mmc {

class Manager {
public:
    // Maps the controller at `pa`, whose card clock source runs at
    // `clock_hz`, identifies its card and registers it as a block device.
    static Error attach(uintptr_t pa, uint64_t clock_hz);

private:
    inline static DwMmcDevice device_{};
    inline static vmm::MmioRegion mapping_{};
    inline static bool attached_{};
};

}  // namespace dw_mmc
