#pragma once

// Numbers from the Bluetooth core specification shared by the host's
// source files, and byte-order helpers: every multi-byte field on the wire is
// least significant byte first.

#include "drivers/bluetooth/hci.h"

namespace bluetooth {

// HCI commands: command group in the top six bits, command in the rest.
inline constexpr uint16_t OP_DISCONNECT = 0x0406;
inline constexpr uint16_t OP_LE_CREATE_CONNECTION = 0x200D;
inline constexpr uint16_t OP_LE_CREATE_CONNECTION_CANCEL = 0x200E;
inline constexpr uint16_t OP_LE_CONNECTION_UPDATE = 0x2013;
inline constexpr uint16_t OP_LE_ENCRYPT = 0x2017;
inline constexpr uint16_t OP_LE_RAND = 0x2018;
inline constexpr uint16_t OP_LE_ENABLE_ENCRYPTION = 0x2019;
inline constexpr uint16_t OP_LE_SET_SCAN_PARAMETERS = 0x200B;
inline constexpr uint16_t OP_LE_SET_SCAN_ENABLE = 0x200C;

inline constexpr uint8_t EVENT_DISCONNECTION_COMPLETE = 0x05;
inline constexpr uint8_t EVENT_ENCRYPTION_CHANGE = 0x08;
inline constexpr uint8_t EVENT_PACKETS_COMPLETED = 0x13;
inline constexpr uint8_t EVENT_LE_META = 0x3E;
inline constexpr uint8_t LE_CONNECTION_COMPLETE = 0x01;
inline constexpr uint8_t STATUS_SUCCESS = 0;
inline constexpr uint8_t REASON_USER_TERMINATED = 0x13;

inline constexpr uint8_t LE_SCAN_ON[] = {1, 1};  // Enabled, duplicates filtered
inline constexpr uint8_t LE_SCAN_OFF[] = {0, 0};

// ACL data: the handle's top bits carry the packet boundary flag.
inline constexpr uint16_t ACL_HANDLE_MASK = 0x0FFF;
inline constexpr uint16_t ACL_BOUNDARY_SHIFT = 12;
inline constexpr uint16_t ACL_BOUNDARY_MASK = 3;
inline constexpr uint16_t ACL_FIRST_NON_FLUSHABLE = 0;  // Host to controller, LE
inline constexpr uint16_t ACL_CONTINUING = 1;
inline constexpr uint16_t ACL_FIRST = 2;  // Controller to host
inline constexpr size_t ACL_HEADER = 4;
inline constexpr size_t L2CAP_HEADER = 4;  // Length, channel

// LE fixed L2CAP channels.
inline constexpr uint16_t CHANNEL_ATT = 0x0004;
inline constexpr uint16_t CHANNEL_SIGNALING = 0x0005;
inline constexpr uint16_t CHANNEL_SMP = 0x0006;

// ATT opcodes.
inline constexpr uint8_t ATT_ERROR = 0x01;
inline constexpr uint8_t ATT_MTU_REQUEST = 0x02;
inline constexpr uint8_t ATT_MTU_RESPONSE = 0x03;
inline constexpr uint8_t ATT_FIND_INFORMATION_REQUEST = 0x04;
inline constexpr uint8_t ATT_FIND_INFORMATION_RESPONSE = 0x05;
inline constexpr uint8_t ATT_FIND_BY_TYPE_REQUEST = 0x06;
inline constexpr uint8_t ATT_READ_BY_TYPE_REQUEST = 0x08;
inline constexpr uint8_t ATT_READ_BY_TYPE_RESPONSE = 0x09;
inline constexpr uint8_t ATT_READ_REQUEST = 0x0A;
inline constexpr uint8_t ATT_READ_RESPONSE = 0x0B;
inline constexpr uint8_t ATT_READ_BLOB_REQUEST = 0x0C;
inline constexpr uint8_t ATT_READ_BLOB_RESPONSE = 0x0D;
inline constexpr uint8_t ATT_READ_BY_GROUP_REQUEST = 0x10;
inline constexpr uint8_t ATT_READ_BY_GROUP_RESPONSE = 0x11;
inline constexpr uint8_t ATT_WRITE_REQUEST = 0x12;
inline constexpr uint8_t ATT_WRITE_RESPONSE = 0x13;
inline constexpr uint8_t ATT_NOTIFICATION = 0x1B;
inline constexpr uint8_t ATT_INDICATION = 0x1D;
inline constexpr uint8_t ATT_CONFIRMATION = 0x1E;
inline constexpr uint8_t ATT_WRITE_COMMAND = 0x52;
inline constexpr uint8_t ATT_SIGNED_WRITE_COMMAND = 0xD2;
inline constexpr uint8_t ATT_ERROR_REQUEST_NOT_SUPPORTED = 0x06;
inline constexpr uint8_t ATT_ERROR_NOT_FOUND = 0x0A;
inline constexpr uint16_t ATT_DEFAULT_MTU = 23;

inline uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

inline void write16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

// A device address as text, most significant byte first.
struct AddressText {
    char text[3 * ADDRESS_BYTES];
};

AddressText address_text(const uint8_t* address);

}  // namespace bluetooth
