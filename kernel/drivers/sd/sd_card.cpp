#include "drivers/sd/sd_card.h"
#include <sys/algorithm.hpp>
#include <sys/iterator.hpp>
#include <kernel/log.h>
#include "lib/stdio.h"
#include <asm/arch.h>

namespace {

constexpr uint8_t CMD0_GO_IDLE = 0;
constexpr uint8_t CMD2_ALL_SEND_CID = 2;
constexpr uint8_t CMD3_SEND_RCA = 3;
constexpr uint8_t ACMD6_SET_BUS_WIDTH = 6;
constexpr uint8_t CMD7_SELECT = 7;
constexpr uint8_t CMD8_SEND_IF_COND = 8;
constexpr uint8_t CMD9_SEND_CSD = 9;
constexpr uint8_t CMD12_STOP_TRANSMISSION = 12;
constexpr uint8_t CMD13_SEND_STATUS = 13;
constexpr uint8_t CMD16_SET_BLOCKLEN = 16;
constexpr uint8_t CMD17_READ_SINGLE = 17;
constexpr uint8_t CMD24_WRITE_SINGLE = 24;
constexpr uint8_t CMD25_WRITE_MULTIPLE = 25;
constexpr uint8_t ACMD41_SEND_OP_COND = 41;
constexpr uint8_t CMD55_APP = 55;

constexpr uint32_t IF_COND_CHECK = 0x1AA;        // 2.7-3.6 V, check pattern 0xAA
constexpr uint32_t ACMD41_HCS = 1U << 30;        // Host supports high capacity
constexpr uint32_t ACMD41_VOLTAGE = 0x00FF8000;  // 2.7-3.6 V
constexpr uint32_t OCR_BUSY = 1U << 31;          // Set when power-up is complete
constexpr uint32_t OCR_CCS = 1U << 30;           // Block-addressed (SDHC/SDXC)
constexpr uint32_t BUS_WIDTH_4 = 2;

constexpr uint32_t CARD_STATUS_ERRORS = 0xFFF9A008;
constexpr uint32_t CARD_READY_FOR_DATA = 1U << 8;
constexpr uint32_t CARD_STATE_MASK = 0xFU << 9;
constexpr uint32_t CARD_STATE_TRANSFER = 4U << 9;

constexpr uint32_t TRANSFER_CLOCK_HZ = 25000000;  // Default-speed limit
constexpr int OP_COND_RETRIES = 1000;
constexpr int STATUS_POLL_LIMIT = 1000000;  // Poll budget; not a wall-clock duration
// Blocks one multi-block write carries. A polling host holds interrupts off
// while it moves them, so the run stays short.
constexpr size_t MULTI_BLOCK_LIMIT = 32;
// Card resets one request may spend on failed blocks before the card goes offline.
constexpr int RECOVERY_ATTEMPTS = 1;

}  // namespace

Error SdCard::command(uint8_t index, uint32_t argument, SdResponseType response, uint32_t* reply) {
    SdCommand cmd{};
    cmd.index = index;
    cmd.argument = argument;
    cmd.response = response;
    TRY(host_.execute(cmd));
    if (reply)
        *reply = cmd.reply[0];
    return Error::None;
}

// CMD9 is valid in the stand-by state, after CMD3 and before CMD7.
Error SdCard::read_csd() {
    SdCommand cmd{};
    cmd.index = CMD9_SEND_CSD;
    cmd.argument = static_cast<uint32_t>(rca_) << 16;
    cmd.response = SdResponseType::R2;
    TRY_LOG(host_.execute(cmd), "sd: CMD9 failed");
    const uint32_t* csd = cmd.reply;  // csd[0] = CSD[127:96] ... csd[3] = CSD[31:0]

    uint32_t structure = csd[0] >> 30;
    if (structure == 1) {
        // CSD 2.0: capacity = (C_SIZE + 1) * 512 KiB, C_SIZE = CSD[69:48].
        uint32_t c_size = ((csd[1] & 0x3F) << 16) | (csd[2] >> 16);
        block_count = (c_size + 1) * 1024;
    } else if (structure == 0) {
        // CSD 1.0: READ_BL_LEN = CSD[83:80], C_SIZE = CSD[73:62], C_SIZE_MULT = CSD[49:47].
        uint32_t read_bl_len = (csd[1] >> 16) & 0xF;
        uint32_t c_size = ((csd[1] & 0x3FF) << 2) | (csd[2] >> 30);
        uint32_t c_size_mult = (csd[2] >> 15) & 0x7;
        uint32_t blocknr = (c_size + 1) << (c_size_mult + 2);
        block_count = (blocknr * (1U << read_bl_len)) / 512;
    } else {
        klog::warn("sd: unknown CSD structure %u, defaulting to 131072 sectors\n", structure);
        block_count = 131072;
    }
    klog::debug("sd: CSD %u.0, %u sectors (%u MB)\n", structure + 1, block_count, block_count / 2048);
    return Error::None;
}

Error SdCard::identify() {
    TRY_LOG(command(CMD0_GO_IDLE, 0, SdResponseType::None), "sd: CMD0 failed");

    uint32_t if_cond{};
    TRY_LOG(command(CMD8_SEND_IF_COND, IF_COND_CHECK, SdResponseType::R7, &if_cond),
            "sd: CMD8 failed (not an SD 2.0 card?)");
    if ((if_cond & 0xFF) != (IF_COND_CHECK & 0xFF)) {
        klog::error("sd: CMD8 check pattern mismatch: 0x%x\n", if_cond);
        return Error::Io;
    }

    sdhc_ = false;
    int retry{};
    for (retry = 0; retry < OP_COND_RETRIES; retry++) {
        TRY_LOG(command(CMD55_APP, 0, SdResponseType::R1), "sd: CMD55 failed");
        uint32_t ocr{};
        TRY_LOG(command(ACMD41_SEND_OP_COND, ACMD41_HCS | ACMD41_VOLTAGE, SdResponseType::R3, &ocr),
                "sd: ACMD41 failed");
        if ((ocr & OCR_BUSY) != 0) {
            sdhc_ = (ocr & OCR_CCS) != 0;
            klog::debug("sd: card ready, %s\n", sdhc_ ? "SDHC/SDXC" : "SDSC");
            break;
        }
    }
    if (retry == OP_COND_RETRIES) {
        klog::error("sd: ACMD41 timeout, card not ready\n");
        return Error::Timeout;
    }

    SdCommand cid{};
    cid.index = CMD2_ALL_SEND_CID;
    cid.response = SdResponseType::R2;
    TRY_LOG(host_.execute(cid), "sd: CMD2 failed");
    for (size_t i = 0; i < sys::size(cid_); ++i)
        cid_[i] = cid.reply[i];

    uint32_t published{};
    TRY_LOG(command(CMD3_SEND_RCA, 0, SdResponseType::R6, &published), "sd: CMD3 failed");
    rca_ = static_cast<uint16_t>(published >> 16);
    klog::debug("sd: RCA = 0x%x\n", rca_);

    TRY(read_csd());
    TRY_LOG(command(CMD7_SELECT, static_cast<uint32_t>(rca_) << 16, SdResponseType::R1b), "sd: CMD7 failed");
    // SDSC cards take byte addresses with a set block length; SDHC ignores it.
    TRY_LOG(command(CMD16_SET_BLOCKLEN, SdCommand::BLOCK_BYTES, SdResponseType::R1), "sd: CMD16 failed");
    return Error::None;
}

// Every SD card supports a 4-bit bus and 25 MHz; hosts opt in to each.
Error SdCard::configure_bus() {
    wide_bus_ = false;
    if (host_.supports_wide_bus()) {
        TRY_LOG(command(CMD55_APP, static_cast<uint32_t>(rca_) << 16, SdResponseType::R1), "sd: CMD55 failed");
        TRY_LOG(command(ACMD6_SET_BUS_WIDTH, BUS_WIDTH_4, SdResponseType::R1), "sd: ACMD6 failed");
        TRY(host_.set_wide_bus());
        wide_bus_ = true;
    }
    return host_.set_transfer_clock(TRANSFER_CLOCK_HZ);
}

Error SdCard::start_locked(const char* prefix, int index) {
    state_ = blk::DeviceState::Initializing;
    Error error = host_.start();
    if (error == Error::None)
        error = identify();
    if (error == Error::None)
        error = configure_bus();
    if (error != Error::None) {
        shutdown_locked();
        return error;
    }
    type = blk::DeviceType::Disk;
    size_t length = 0;
    for (; prefix[length] && length < sizeof(name) - 2; ++length)
        name[length] = prefix[length];
    name[length] = static_cast<char>('0' + index);
    name[length + 1] = '\0';
    state_ = blk::DeviceState::Ready;
    klog::debug("sd: card initialized as '%s'\n", name);
    return Error::None;
}

void SdCard::shutdown() {
    sys::lock_guard<Mutex> guard(io_mutex_);
    shutdown_locked();
}

void SdCard::shutdown_locked() {
    host_.stop();
    rca_ = 0;
    sdhc_ = false;
    wide_bus_ = false;
    for (auto& word : cid_)
        word = 0;
    block_count = 0;
    state_ = blk::DeviceState::Offline;
}

// A data error can be transient: a glitch on the bus, or a host that timed out
// while the CPU was held elsewhere. Reset the controller and identify the card
// again. It must be the same card with the same capacity and addressing;
// anything else could put blocks of one card at the wrong place on another.
Error SdCard::recover_locked() {
    const uint32_t capacity = block_count;
    const bool sdhc = sdhc_;
    constexpr size_t cid_words = sizeof(cid_) / sizeof(cid_[0]);
    uint32_t cid[cid_words];
    for (size_t i = 0; i < cid_words; ++i)
        cid[i] = cid_[i];
    TRY(host_.start());
    TRY(identify());
    bool same = block_count == capacity && sdhc_ == sdhc;
    for (size_t i = 0; i < cid_words; ++i)
        same = same && cid_[i] == cid[i];
    ENSURE_LOG(same, Error::Io, "sd: %s: a different card answered after reset", name);
    return configure_bus();
}

Error SdCard::read_blocks(uint32_t start_lba, void* buf, size_t count) {
    return transfer_blocks(start_lba, buf, count, false);
}

Error SdCard::write_blocks(uint32_t start_lba, const void* buf, size_t count) {
    return transfer_blocks(start_lba, const_cast<void*>(buf), count, true);
}

Error SdCard::flush_cache() {
    sys::lock_guard<Mutex> guard(io_mutex_);
    ENSURE(state_ == blk::DeviceState::Ready, Error::NoDevice);
    // CMD0 resets the card and the optional SD cache is never enabled. Wait
    // for programming to finish; transfer completion alone is insufficient.
    for (int polls = STATUS_POLL_LIMIT; polls > 0; --polls) {
        uint32_t status{};
        TRY(command(CMD13_SEND_STATUS, static_cast<uint32_t>(rca_) << 16, SdResponseType::R1, &status));
        ENSURE((status & CARD_STATUS_ERRORS) == 0, Error::Io);
        if ((status & CARD_READY_FOR_DATA) != 0 && (status & CARD_STATE_MASK) == CARD_STATE_TRANSFER)
            return Error::None;
        arch_spin_hint();
    }
    return Error::Timeout;
}

// A card that has to program every block before it accepts the next spends
// most of a single-block write busy. Consecutive blocks therefore go out as
// one multi-block write, which lets the card take them as a run.
Error SdCard::transfer_run(uint32_t lba, uint8_t* data, size_t blocks, bool write) {
    SdCommand cmd{};
    cmd.index = !write ? CMD17_READ_SINGLE : blocks == 1 ? CMD24_WRITE_SINGLE : CMD25_WRITE_MULTIPLE;
    cmd.argument = sdhc_ ? lba : lba * SdCommand::BLOCK_BYTES;
    cmd.response = SdResponseType::R1;
    cmd.data = data;
    cmd.blocks = static_cast<uint32_t>(blocks);
    cmd.write = write;
    TRY(host_.execute(cmd));
    if (blocks == 1)
        return Error::None;
    // The card keeps receiving until told to stop, then programs the last block.
    return command(CMD12_STOP_TRANSMISSION, 0, SdResponseType::R1b);
}

Error SdCard::transfer_blocks(uint32_t start_lba, void* buf, size_t count, bool write) {
    sys::lock_guard<Mutex> guard(io_mutex_);
    ENSURE(state_ == blk::DeviceState::Ready, Error::NoDevice);
    ENSURE(start_lba <= block_count && count <= block_count - start_lba, Error::Invalid);
    ENSURE(buf || count == 0, Error::Invalid);
    ENSURE(sdhc_ || (start_lba <= (1U << 23) && count <= (1U << 23) - start_lba), Error::Invalid);
    auto* data = static_cast<uint8_t*>(buf);
    int recoveries = RECOVERY_ATTEMPTS;
    for (size_t i = 0; i < count;) {
        const size_t blocks = write ? sys::min(count - i, MULTI_BLOCK_LIMIT) : 1;
        const uint32_t lba = start_lba + static_cast<uint32_t>(i);
        uint8_t* run = data + i * SdCommand::BLOCK_BYTES;
        Error error = transfer_run(lba, run, blocks, write);
        if (error != Error::None && recoveries > 0) {
            --recoveries;
            klog::warn("sd: %s: block %u %s failed (%d), resetting the card\n", name, lba, write ? "write" : "read",
                       static_cast<int>(error));
            // The whole run is sent again: the card may have taken part of it.
            Error recovered = recover_locked();
            if (recovered == Error::None) {
                error = transfer_run(lba, run, blocks, write);
            } else {
                klog::error("sd: %s: reset failed (%d)\n", name, static_cast<int>(recovered));
            }
        }
        if (error != Error::None) {
            // A card that fails again after a reset stays offline until it
            // is explicitly started again.
            klog::error("sd: %s: taking the card offline\n", name);
            shutdown_locked();
            return error;
        }
        i += blocks;
    }
    return Error::None;
}

void SdCard::print_info() {
    cprintf("SD card '%s': %s, %s bus, RCA=0x%x, %d sectors (%d MB)\n", name, sdhc_ ? "SDHC" : "SDSC",
            wide_bus_ ? "4-bit" : "1-bit", rca_, block_count, block_count / 2048);
}
