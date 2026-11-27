#include "drivers/sd/dw_mmc.h"
#include <kernel/log.h>
#include "drivers/intr.h"
#include "drivers/mmio.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include <asm/arch.h>

namespace {

namespace reg {

constexpr uint32_t CTRL = 0x00;
constexpr uint32_t PWREN = 0x04;
constexpr uint32_t CLKDIV = 0x08;
constexpr uint32_t CLKSRC = 0x0C;
constexpr uint32_t CLKENA = 0x10;
constexpr uint32_t TMOUT = 0x14;
constexpr uint32_t CTYPE = 0x18;
constexpr uint32_t BLKSIZ = 0x1C;
constexpr uint32_t BYTCNT = 0x20;
constexpr uint32_t INTMASK = 0x24;
constexpr uint32_t CMDARG = 0x28;
constexpr uint32_t CMD = 0x2C;
constexpr uint32_t RESP0 = 0x30;
constexpr uint32_t RINTSTS = 0x44;
constexpr uint32_t STATUS = 0x48;
constexpr uint32_t VERID = 0x6C;
constexpr uint32_t UHS_REG = 0x74;
constexpr uint32_t BMOD = 0x80;

}  // namespace reg

constexpr uint32_t CTRL_RESET = 1U << 0;
constexpr uint32_t CTRL_FIFO_RESET = 1U << 1;
constexpr uint32_t CTRL_DMA_RESET = 1U << 2;
constexpr uint32_t CTRL_INT_ENABLE = 1U << 4;
constexpr uint32_t CTRL_DMA_ENABLE = 1U << 5;
constexpr uint32_t CTRL_USE_IDMAC = 1U << 25;
constexpr uint32_t CTRL_ALL_RESETS = CTRL_RESET | CTRL_FIFO_RESET | CTRL_DMA_RESET;

constexpr uint32_t CLKENA_ENABLE = 1U << 0;
constexpr uint32_t CTYPE_4BIT = 1U << 0;

constexpr uint32_t CMD_START = 1U << 31;
constexpr uint32_t CMD_USE_HOLD_REG = 1U << 29;
constexpr uint32_t CMD_UPDATE_CLOCK = 1U << 21;
constexpr uint32_t CMD_INIT = 1U << 15;
constexpr uint32_t CMD_STOP_ABORT = 1U << 14;
constexpr uint32_t CMD_PRV_DAT_WAIT = 1U << 13;
constexpr uint32_t CMD_DAT_WRITE = 1U << 10;
constexpr uint32_t CMD_DAT_EXPECTED = 1U << 9;
constexpr uint32_t CMD_RESP_CRC = 1U << 8;
constexpr uint32_t CMD_RESP_LONG = 1U << 7;
constexpr uint32_t CMD_RESP_EXPECTED = 1U << 6;

constexpr uint32_t INT_RESP_ERROR = 1U << 1;
constexpr uint32_t INT_CMD_DONE = 1U << 2;
constexpr uint32_t INT_DATA_OVER = 1U << 3;
constexpr uint32_t INT_RESP_CRC = 1U << 6;
constexpr uint32_t INT_DATA_CRC = 1U << 7;
constexpr uint32_t INT_RESP_TIMEOUT = 1U << 8;
constexpr uint32_t INT_DATA_TIMEOUT = 1U << 9;
constexpr uint32_t INT_HOST_TIMEOUT = 1U << 10;
constexpr uint32_t INT_FIFO_ERROR = 1U << 11;
constexpr uint32_t INT_HARDWARE_LOCKED = 1U << 12;
constexpr uint32_t INT_START_BIT = 1U << 13;
constexpr uint32_t INT_END_BIT = 1U << 15;
constexpr uint32_t INT_ALL = 0xFFFFFFFF;
constexpr uint32_t INT_COMMAND_ERRORS = INT_RESP_ERROR | INT_RESP_CRC | INT_RESP_TIMEOUT | INT_HARDWARE_LOCKED;
constexpr uint32_t INT_DATA_ERRORS =
    INT_DATA_CRC | INT_DATA_TIMEOUT | INT_HOST_TIMEOUT | INT_FIFO_ERROR | INT_START_BIT | INT_END_BIT;

constexpr uint32_t STATUS_FIFO_FULL = 1U << 3;
constexpr uint32_t STATUS_DATA_BUSY = 1U << 9;
constexpr uint32_t STATUS_FIFO_COUNT_SHIFT = 17;
constexpr uint32_t STATUS_FIFO_COUNT_MASK = 0x1FFF;

// Controllers from version 2.40a move the data FIFO from 0x100 to 0x200.
constexpr uint32_t VERID_FIFO_MOVED = 0x240A;
constexpr uint32_t FIFO_OLD = 0x100;
constexpr uint32_t FIFO_NEW = 0x200;

constexpr uint32_t IDENTIFY_CLOCK_HZ = 400000;
constexpr int POLL_LIMIT = 1000000;  // Poll budget; not a wall-clock duration
constexpr size_t BLOCK_WORDS = SdCommand::BLOCK_BYTES / sizeof(uint32_t);
constexpr uint8_t STOP_TRANSMISSION = 12;  // CMD12, which ends a multi-block transfer

}  // namespace

Error DwMmcHost::wait_status(uint32_t mask, const char* what) {
    for (int i = 0; i < POLL_LIMIT; i++) {
        if ((mmio::read32(base_, reg::STATUS) & mask) == 0)
            return Error::None;
        arch_spin_hint();
    }
    klog::error("dw_mmc: %s timeout\n", what);
    return Error::Timeout;
}

Error DwMmcHost::reset() {
    uint32_t ctrl = mmio::read32(base_, reg::CTRL);
    mmio::write32(base_, reg::CTRL, ctrl | CTRL_ALL_RESETS);
    for (int i = 0; i < POLL_LIMIT; i++) {
        if ((mmio::read32(base_, reg::CTRL) & CTRL_ALL_RESETS) == 0)
            return Error::None;
    }
    klog::error("dw_mmc: reset timeout\n");
    return Error::Timeout;
}

// Clock register writes take effect only through an update-clock command.
Error DwMmcHost::update_clock() {
    mmio::write32(base_, reg::CMD, CMD_START | CMD_UPDATE_CLOCK | CMD_PRV_DAT_WAIT);
    for (int i = 0; i < POLL_LIMIT; i++) {
        if ((mmio::read32(base_, reg::CMD) & CMD_START) == 0)
            return Error::None;
        if ((mmio::read32(base_, reg::RINTSTS) & INT_HARDWARE_LOCKED) != 0) {
            mmio::write32(base_, reg::RINTSTS, INT_HARDWARE_LOCKED);
            klog::error("dw_mmc: clock update rejected\n");
            return Error::Io;
        }
    }
    klog::error("dw_mmc: clock update timeout\n");
    return Error::Timeout;
}

// The card clock is clock_hz_ / (2 * CLKDIV), or clock_hz_ itself when CLKDIV is 0.
Error DwMmcHost::set_clock(uint32_t rate_hz) {
    uint64_t divider = 0;
    if (clock_hz_ > rate_hz)
        divider = (clock_hz_ + 2ULL * rate_hz - 1) / (2ULL * rate_hz);
    ENSURE(divider <= 0xFF, Error::Invalid);
    mmio::write32(base_, reg::CLKENA, 0);
    TRY(update_clock());
    mmio::write32(base_, reg::CLKSRC, 0);
    mmio::write32(base_, reg::CLKDIV, static_cast<uint32_t>(divider));
    TRY(update_clock());
    mmio::write32(base_, reg::CLKENA, CLKENA_ENABLE);
    TRY(update_clock());
    uint64_t actual = divider == 0 ? clock_hz_ : clock_hz_ / (2 * divider);
    klog::debug("dw_mmc: card clock %lu Hz (divider %lu)\n", actual, divider);
    return Error::None;
}

Error DwMmcHost::start() {
    ENSURE(base_ && clock_hz_ != 0, Error::Invalid);
    uint32_t version = mmio::read32(base_, reg::VERID) & 0xFFFF;
    data_offset_ = version >= VERID_FIFO_MOVED ? FIFO_NEW : FIFO_OLD;
    klog::debug("dw_mmc: controller version 0x%x\n", version);

    TRY(reset());
    uint32_t ctrl = mmio::read32(base_, reg::CTRL);
    mmio::write32(base_, reg::CTRL, ctrl & ~(CTRL_INT_ENABLE | CTRL_DMA_ENABLE | CTRL_USE_IDMAC));
    mmio::write32(base_, reg::BMOD, 0);
    mmio::write32(base_, reg::INTMASK, 0);
    mmio::write32(base_, reg::RINTSTS, INT_ALL);
    mmio::write32(base_, reg::TMOUT, 0xFFFFFFFF);
    mmio::write32(base_, reg::UHS_REG, 0);
    mmio::write32(base_, reg::CTYPE, 0);
    mmio::write32(base_, reg::PWREN, 1);
    return set_clock(IDENTIFY_CLOCK_HZ);
}

void DwMmcHost::stop() {
    if (!base_) {
        return;
    }
    mmio::write32(base_, reg::INTMASK, 0);
    mmio::write32(base_, reg::CLKENA, 0);
    static_cast<void>(update_clock());
    mmio::write32(base_, reg::PWREN, 0);
    base_ = nullptr;
}

Error DwMmcHost::set_wide_bus() {
    mmio::write32(base_, reg::CTYPE, CTYPE_4BIT);
    return Error::None;
}

Error DwMmcHost::set_transfer_clock(uint32_t rate_hz) {
    return set_clock(rate_hz);
}

Error DwMmcHost::wait_idle() {
    return wait_status(STATUS_DATA_BUSY, "card busy");
}

Error DwMmcHost::move_data(SdCommand& command) {
    auto* bytes = static_cast<uint8_t*>(command.data);
    const size_t words = command.blocks * BLOCK_WORDS;
    size_t moved = 0;
    for (int polls = 0; polls < POLL_LIMIT; polls++) {
        uint32_t pending = mmio::read32(base_, reg::RINTSTS);
        if ((pending & INT_DATA_ERRORS) != 0) {
            mmio::write32(base_, reg::RINTSTS, INT_ALL);
            klog::error("dw_mmc: CMD%u data error 0x%x\n", command.index, pending);
            return Error::Io;
        }
        uint32_t status = mmio::read32(base_, reg::STATUS);
        if (command.write) {
            while (moved < words && (status & STATUS_FIFO_FULL) == 0) {
                uint32_t word{};
                memcpy(&word, bytes + moved * sizeof(word), sizeof(word));
                mmio::write32(base_, data_offset_, word);
                ++moved;
                status = mmio::read32(base_, reg::STATUS);
            }
        } else {
            size_t available = (status >> STATUS_FIFO_COUNT_SHIFT) & STATUS_FIFO_COUNT_MASK;
            for (; available > 0 && moved < words; --available, ++moved) {
                uint32_t word = mmio::read32(base_, data_offset_);
                memcpy(bytes + moved * sizeof(word), &word, sizeof(word));
            }
        }
        if (moved == words && (pending & INT_DATA_OVER) != 0) {
            mmio::write32(base_, reg::RINTSTS, INT_ALL);
            return Error::None;
        }
    }
    klog::error("dw_mmc: CMD%u data timeout after %lu words\n", command.index, moved);
    return Error::Timeout;
}

Error DwMmcHost::execute(SdCommand& command) {
    uint32_t flags = CMD_START | CMD_USE_HOLD_REG | command.index;
    switch (command.response) {
        case SdResponseType::None: break;
        case SdResponseType::R2: flags |= CMD_RESP_EXPECTED | CMD_RESP_LONG | CMD_RESP_CRC; break;
        case SdResponseType::R3: flags |= CMD_RESP_EXPECTED; break;
        case SdResponseType::R1:
        case SdResponseType::R1b:
        case SdResponseType::R6:
        case SdResponseType::R7: flags |= CMD_RESP_EXPECTED | CMD_RESP_CRC; break;
    }
    if (command.index == 0)
        flags |= CMD_INIT;  // Send the 80 initialization clocks first.
    if (command.index == STOP_TRANSMISSION)
        flags |= CMD_STOP_ABORT;  // Returns the data state machine to idle.

    TRY(wait_idle());
    mmio::write32(base_, reg::RINTSTS, INT_ALL);
    if (command.data) {
        uint32_t ctrl = mmio::read32(base_, reg::CTRL);
        mmio::write32(base_, reg::CTRL, ctrl | CTRL_FIFO_RESET);
        for (int i = 0; (mmio::read32(base_, reg::CTRL) & CTRL_FIFO_RESET) != 0; i++)
            ENSURE(i < POLL_LIMIT, Error::Timeout);
        ENSURE(command.blocks != 0, Error::Invalid);
        mmio::write32(base_, reg::BLKSIZ, SdCommand::BLOCK_BYTES);
        mmio::write32(base_, reg::BYTCNT, command.blocks * SdCommand::BLOCK_BYTES);
        flags |= CMD_DAT_EXPECTED | CMD_PRV_DAT_WAIT | (command.write ? CMD_DAT_WRITE : 0);
    }
    if (!command.data) {
        TRY(issue(command, flags));
        return command.response == SdResponseType::R1b ? wait_idle() : Error::None;
    }
    {
        // The blocks move through the FIFO by polling. If the task were
        // switched out meanwhile, the card would fill the FIFO and the
        // controller would end the transfer with a host timeout, so nothing
        // may interrupt it until the blocks have been moved.
        intr::Guard guard;
        TRY(issue(command, flags));
        TRY(move_data(command));
    }
    // A write is complete once the card leaves its programming busy state.
    return command.write ? wait_idle() : Error::None;
}

// Sends a command and reads its response.
Error DwMmcHost::issue(SdCommand& command, uint32_t flags) {
    mmio::write32(base_, reg::CMDARG, command.argument);
    mmio::write32(base_, reg::CMD, flags);

    uint32_t pending = 0;
    int polls = 0;
    for (; polls < POLL_LIMIT; polls++) {
        pending = mmio::read32(base_, reg::RINTSTS);
        if ((pending & (INT_CMD_DONE | INT_HARDWARE_LOCKED)) != 0)
            break;
        arch_spin_hint();
    }
    if (polls == POLL_LIMIT) {
        klog::error("dw_mmc: CMD%u timeout\n", command.index);
        return Error::Timeout;
    }
    if ((pending & INT_COMMAND_ERRORS) != 0) {
        mmio::write32(base_, reg::RINTSTS, INT_ALL);
        // ACMD41 polling and probing both meet timeouts in normal operation.
        klog::debug("dw_mmc: CMD%u failed, status 0x%x\n", command.index, pending);
        return (pending & INT_RESP_TIMEOUT) != 0 ? Error::Timeout : Error::Io;
    }
    mmio::write32(base_, reg::RINTSTS, INT_CMD_DONE);

    if (command.response == SdResponseType::R2) {
        // RESP3 holds response bits [127:96] and RESP0 bits [31:0].
        for (int i = 0; i < 4; i++)
            command.reply[i] = mmio::read32(base_, reg::RESP0 + (3 - i) * 4);
    } else if (command.response != SdResponseType::None) {
        command.reply[0] = mmio::read32(base_, reg::RESP0);
    }
    return Error::None;
}

Error DwMmcDevice::init(volatile uint8_t* base, uint64_t clock_hz, int index) {
    ENSURE(base && clock_hz != 0, Error::Invalid);
    return start("mmc", index, [this, base, clock_hz] { host_.attach(base, clock_hz); });
}

namespace dw_mmc {

Error Manager::attach(uintptr_t pa, uint64_t clock_hz) {
    ENSURE(!attached_, Error::Busy);
    auto mapped = vmm::map_mmio(pa, 0x1000, VM_WRITE | VM_NOCACHE);
    if (!mapped.ok())
        return mapped.release_error();
    auto mapping = mapped.release_value();
    Error error = device_.init(reinterpret_cast<volatile uint8_t*>(mapping->address()), clock_hz, 0);
    if (error == Error::None)
        error = blk::register_device(&device_);
    if (error != Error::None) {
        device_.shutdown();
        klog::warn("dw_mmc: no card at 0x%lx (%d)\n", static_cast<uint64_t>(pa), static_cast<int>(error));
        return error;
    }
    mapping_ = sys::move(mapping);
    attached_ = true;
    klog::info("dw_mmc: %s, %u MiB\n", device_.name, device_.block_count / 2048);
    return Error::None;
}

}  // namespace dw_mmc
