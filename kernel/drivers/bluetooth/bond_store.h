#pragma once

// The file that keeps Bluetooth bonds across restarts. A bond is between two
// devices, so each record names the local adapter as well as the bonded
// device, with the key it handed out and where its reports are. A file that
// does not check out is not used: its devices just have to pair again.
//
// Layout, little-endian: "ZBTB", version, record count, two reserved bytes,
// fixed-size records, then an FNV-1a checksum of everything before it.

#include "drivers/bluetooth/hci.h"

namespace bluetooth::bond_store {

inline constexpr size_t MAX_RECORDS = 8;
inline constexpr size_t HEADER_BYTES = 8;
inline constexpr size_t RECORD_BYTES = 86;
inline constexpr size_t CHECKSUM_BYTES = 4;
inline constexpr size_t FILE_CAPACITY = HEADER_BYTES + MAX_RECORDS * RECORD_BYTES + CHECKSUM_BYTES;

struct Record {
    uint8_t local[ADDRESS_BYTES]{};  // The adapter the bond belongs to
    uint16_t appearance{};
    Bond bond{};
};

// Writes `count` records; returns the file's length, or 0 when they do not fit.
size_t encode(const Record* records, size_t count, uint8_t* out, size_t capacity);
// Reads a file's records into `records`; false when it is not a whole,
// intact bond file of this version.
bool decode(const uint8_t* data, size_t length, Record* records, size_t capacity, size_t& count);

}  // namespace bluetooth::bond_store
