#pragma once

// HID report descriptors (USB Device Class Definition for HID 1.11, 6.2.2),
// as far as a host needs them for devices that send no boot reports: the
// report IDs of the keyboard and mouse application collections, and where a
// mouse puts its buttons, movement and wheel in its input report.

#include <base/types.h>

namespace input {

// A field of an input report, in bits from the start of the report, which
// does not include the report ID.
struct ReportField {
    bool present{};
    bool is_signed{};
    uint8_t size{};  // Bits; for buttons, one bit per button
    uint16_t bit{};
};

struct MouseLayout {
    bool valid{};         // Both movements were found
    uint8_t report_id{};  // 0 when the descriptor uses no report IDs
    ReportField buttons{};
    ReportField x{};
    ReportField y{};
    ReportField wheel{};
};

struct ReportMap {
    MouseLayout mouse{};
    bool keyboard{};
    uint8_t keyboard_report_id{};
};

// Reads a report descriptor; what it does not describe stays absent.
ReportMap parse_report_map(const uint8_t* descriptor, size_t length);

// A field's value in a report, sign-extended when the field is signed; bits
// past the end of the report read as zero.
int32_t field_value(const ReportField& field, const uint8_t* report, size_t length);

}  // namespace input
