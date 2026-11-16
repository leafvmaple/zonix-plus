#pragma once

#include "lib/result.h"

namespace ata {

inline constexpr uint8_t CMD_FLUSH_CACHE = 0xE7;
inline constexpr uint8_t CMD_FLUSH_CACHE_EXT = 0xEA;
inline constexpr uint16_t IDENTIFY_VALID_MASK = 0xC000;
inline constexpr uint16_t IDENTIFY_VALID = 0x4000;
inline constexpr uint16_t SUPPORTS_FLUSH = 1U << 12;
inline constexpr uint16_t SUPPORTS_FLUSH_EXT = 1U << 13;
inline constexpr uint16_t SUPPORTS_WRITE_CACHE = 1U << 5;

class CacheFlush {
public:
    void identify(const uint16_t* words) {
        status_ = Error::NotSupported;
        command_ = 0;
        if ((words[83] & IDENTIFY_VALID_MASK) != IDENTIFY_VALID) {
            return;
        }
        if ((words[83] & SUPPORTS_FLUSH_EXT) != 0) {
            command_ = CMD_FLUSH_CACHE_EXT;
        } else if ((words[83] & SUPPORTS_FLUSH) != 0) {
            command_ = CMD_FLUSH_CACHE;
        } else if ((words[82] & SUPPORTS_WRITE_CACHE) != 0) {
            return;
        }
        status_ = Error::None;
    }
    // Zero is a verified device without a write cache; no command is needed.
    Result<uint8_t> command() const {
        if (status_ != Error::None) {
            return status_;
        }
        return command_;
    }

private:
    uint8_t command_{};
    Error status_{Error::NotSupported};
};

}  // namespace ata
