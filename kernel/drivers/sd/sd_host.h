#pragma once

// The boundary between the SD card protocol (SdCard) and a host controller
// driver. SdCard owns the card: identification, addressing, capacity, bus
// setup, block transfers and error handling. A host implements only how its
// controller issues one command, moves one data block and sets its bus.

#include <base/types.h>
#include "lib/result.h"

// Response formats defined by the SD Physical Layer specification.
enum class SdResponseType : uint8_t {
    None,
    R1,   // Card status
    R1b,  // Card status, then busy on DAT0
    R2,   // 136-bit CID or CSD
    R3,   // OCR, without CRC
    R6,   // Published RCA
    R7,   // Interface condition
};

struct SdCommand {
    static constexpr size_t BLOCK_BYTES = 512;

    uint8_t index{};
    uint32_t argument{};
    SdResponseType response{SdResponseType::None};
    // When set, the command transfers one BLOCK_BYTES block to or from data.
    void* data{};
    bool write{};
    // Filled by the host. Short responses: reply[0] holds response bits
    // [39:8]. R2: reply[0..3] hold bits [127:96], [95:64], [63:32], [31:0],
    // whatever layout the controller's registers use.
    uint32_t reply[4]{};
};

class SdHost {
public:
    // Resets the controller, selects an identification clock of at most
    // 400 kHz on a 1-bit bus and powers the card.
    virtual Error start() = 0;
    // Disables signals, the card clock and card power. Never touches
    // registers when the host is not attached.
    virtual void stop() = 0;
    // Issues one command, including its data block, and fills its reply.
    virtual Error execute(SdCommand& command) = 0;
    // Switches the controller to a 4-bit bus after the card has been told to.
    virtual bool supports_wide_bus() const { return false; }
    virtual Error set_wide_bus() { return Error::NotSupported; }
    // Raises the card clock for data transfer. A host may keep its current one.
    virtual Error set_transfer_clock(uint32_t) { return Error::None; }

protected:
    ~SdHost() = default;
};
