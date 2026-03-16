#pragma once

#include <base/types.h>
#include "block/blk.h"

// ==========================================================================
// SDHCI (SD Host Controller Interface) driver
//
// Supports SD cards via the standard SDHCI register set.
// Works with QEMU sdhci-pci and Raspberry Pi Arasan SDHCI.
// ==========================================================================

class SdDevice : public BlockDevice {
public:
    int init(volatile uint8_t* base);
    int read(uint32_t block_number, void* buf, size_t block_count) override;
    int write(uint32_t block_number, const void* buf, size_t block_count) override;
    void print_info() override;
    void test() override;

private:
    volatile uint8_t* m_base{};
    uint16_t m_rca{};  // Relative Card Address
    bool m_sdhc{};     // true if SDHC/SDXC (block addressing)

    // Register access
    uint32_t rd32(uint32_t off);
    void wr32(uint32_t off, uint32_t val);
    uint16_t rd16(uint32_t off);
    void wr16(uint32_t off, uint16_t val);
    uint8_t rd8(uint32_t off);
    void wr8(uint32_t off, uint8_t val);

    // Controller operations
    int reset();
    int clock_setup();
    int power_on();
    int send_cmd(uint8_t index, uint32_t arg, uint16_t flags);
    int wait_cmd_done();
    int wait_xfer_done();
    uint32_t read_response(int reg);

    // SD card protocol
    int card_identify();
    int read_single(uint32_t lba, void* buf);
    int write_single(uint32_t lba, const void* buf);
};

namespace sdhci {

int init();
SdDevice* get_device();

}  // namespace sdhci
