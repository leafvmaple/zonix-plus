#pragma once

#include <base/types.h>
#include "block/blk.h"
#include "drivers/sd/sd_host.h"
#include "lib/mutex.h"
#include "lib/result.h"

// One SD card on one host controller, published as a block device. The
// protocol lives here once; controllers supply an SdHost.
class SdCard : public BlockDevice {
public:
    [[nodiscard]] blk::DeviceState state() const { return state_; }
    void shutdown();
    void print_info() override;

protected:
    explicit SdCard(SdHost& host) : host_(host) {}
    ~SdCard() = default;

    // Attaches the host inside the device lock, then identifies the card and
    // names the device `prefix` followed by `index`.
    template<typename Attach>
    Error start(const char* prefix, int index, Attach&& attach) {
        sys::lock_guard<Mutex> guard(io_mutex_);
        ENSURE(state_ == blk::DeviceState::Offline, Error::Busy);
        attach();
        return start_locked(prefix, index);
    }

private:
    Error read_blocks(uint32_t start_lba, void* buf, size_t block_count) override;
    Error write_blocks(uint32_t start_lba, const void* buf, size_t block_count) override;
    Error flush_cache() override;

    Error start_locked(const char* prefix, int index);
    void shutdown_locked();
    Error recover_locked();
    Error command(uint8_t index, uint32_t argument, SdResponseType response, uint32_t* reply = nullptr);
    Error identify();
    Error read_csd();
    Error configure_bus();
    Error transfer_blocks(uint32_t start_lba, void* buf, size_t count, bool write);

    SdHost& host_;
    Mutex io_mutex_{};
    blk::DeviceState state_{blk::DeviceState::Offline};
    uint32_t cid_[4]{};  // Card identification, to recognise the card after a reset
    uint16_t rca_{};
    bool sdhc_{};
    bool wide_bus_{};
};
