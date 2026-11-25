#include "test/test_defs.h"
#include "drivers/hid_report.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

// A keyboard (report 1) and a mouse (report 2) in one descriptor, as BLE
// mice with a keyboard function send it: five buttons and three bits of
// padding, 12-bit X and Y, and an 8-bit wheel.
// clang-format off
constexpr uint8_t DESCRIPTOR[] = {
    // Keyboard: modifiers, a reserved byte and six key usages.
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
    0x25, 0x65, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
    // Mouse: a pointer collection inside the application collection.
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01, 0x05, 0x01,
    0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x06, 0x15, 0x81,
    0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x09, 0x38, 0x81, 0x06, 0xC0, 0xC0,
};
// clang-format on

// Left button, X -3, Y +5, wheel -1, laid out as the descriptor says.
constexpr uint8_t MOUSE_REPORT[] = {0x01, 0xFD, 0x5F, 0x00, 0xFF};

void test_mouse_layout() {
    TEST_START("HID report descriptor: mouse layout and keyboard report");
    const input::ReportMap map = input::parse_report_map(DESCRIPTOR, sizeof(DESCRIPTOR));
    const input::MouseLayout& mouse = map.mouse;
    TEST_ASSERT(map.keyboard && map.keyboard_report_id == 1, "The keyboard collection sends report 1");
    TEST_ASSERT(mouse.valid && mouse.report_id == 2, "The mouse collection sends report 2");
    // Offsets count from the start of report 2, past the padding, not
    // from the keyboard's report before it.
    TEST_ASSERT(mouse.buttons.bit == 0 && mouse.buttons.size == 5, "Five buttons start the report");
    TEST_ASSERT(mouse.x.bit == 8 && mouse.x.size == 12 && mouse.x.is_signed, "X is 12 signed bits after padding");
    TEST_ASSERT(mouse.y.bit == 20 && mouse.y.size == 12, "Y follows X");
    TEST_ASSERT(mouse.wheel.bit == 32 && mouse.wheel.size == 8, "The wheel follows Y");
    TEST_ASSERT(input::field_value(mouse.buttons, MOUSE_REPORT, sizeof(MOUSE_REPORT)) == 1 &&
                    input::field_value(mouse.x, MOUSE_REPORT, sizeof(MOUSE_REPORT)) == -3 &&
                    input::field_value(mouse.y, MOUSE_REPORT, sizeof(MOUSE_REPORT)) == 5 &&
                    input::field_value(mouse.wheel, MOUSE_REPORT, sizeof(MOUSE_REPORT)) == -1,
                "A report decodes to its buttons, signed movement and wheel");
    TEST_ASSERT(input::field_value(mouse.wheel, MOUSE_REPORT, 4) == 0, "Fields past a short report read as zero");
    TEST_END();
}

void test_truncated_descriptor() {
    TEST_START("HID report descriptor: truncated");
    // Cut inside the mouse's X/Y item: the movement is never declared.
    const input::ReportMap map = input::parse_report_map(DESCRIPTOR, sizeof(DESCRIPTOR) - 22);
    TEST_ASSERT(map.keyboard && !map.mouse.valid, "A mouse without both movements is not a usable layout");
    TEST_END();
}

}  // namespace

namespace hid_report_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;
    test_mouse_layout();
    test_truncated_descriptor();
    TEST_SUMMARY("HID reports");
}

}  // namespace hid_report_test
