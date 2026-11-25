#include "drivers/hid_report.h"

namespace input {
namespace {

// Short item prefix: data size in the low two bits (3 means four bytes),
// item type, then tag.
constexpr uint8_t ITEM_LONG = 0xFE;
constexpr uint8_t TYPE_MAIN = 0;
constexpr uint8_t TYPE_GLOBAL = 1;
constexpr uint8_t TYPE_LOCAL = 2;

constexpr uint8_t MAIN_INPUT = 0x8;
constexpr uint8_t MAIN_COLLECTION = 0xA;
constexpr uint8_t MAIN_END_COLLECTION = 0xC;
constexpr uint8_t GLOBAL_USAGE_PAGE = 0x0;
constexpr uint8_t GLOBAL_LOGICAL_MINIMUM = 0x1;
constexpr uint8_t GLOBAL_REPORT_SIZE = 0x7;
constexpr uint8_t GLOBAL_REPORT_ID = 0x8;
constexpr uint8_t GLOBAL_REPORT_COUNT = 0x9;
constexpr uint8_t LOCAL_USAGE = 0x0;
constexpr uint8_t LOCAL_USAGE_MINIMUM = 0x1;
constexpr uint8_t LOCAL_USAGE_MAXIMUM = 0x2;

constexpr uint32_t INPUT_CONSTANT = 1U << 0;
constexpr uint32_t COLLECTION_APPLICATION = 1;

constexpr uint16_t PAGE_GENERIC_DESKTOP = 0x01;
constexpr uint16_t PAGE_BUTTON = 0x09;
constexpr uint16_t USAGE_MOUSE = 0x02;
constexpr uint16_t USAGE_KEYBOARD = 0x06;
constexpr uint16_t USAGE_X = 0x30;
constexpr uint16_t USAGE_Y = 0x31;
constexpr uint16_t USAGE_WHEEL = 0x38;
constexpr uint8_t MAX_BUTTONS = 8;

constexpr size_t MAX_USAGES = 16;
constexpr size_t REPORT_IDS = 256;
constexpr uint32_t PAGE_SHIFT = 16;  // An extended usage carries its page above the usage

// Items that apply to the next main item only.
struct Locals {
    uint32_t usages[MAX_USAGES]{};  // Extended: page above, usage below
    size_t usage_count{};
    uint32_t minimum{};
    uint32_t maximum{};
    bool range{};
};

uint32_t extended(uint32_t usage, size_t size, uint16_t page) {
    return size == 4 ? usage : (static_cast<uint32_t>(page) << PAGE_SHIFT) | usage;
}

uint32_t usage_at(const Locals& locals, size_t index) {
    if (locals.range) {
        const uint32_t usage = locals.minimum + static_cast<uint32_t>(index);
        return usage <= locals.maximum ? usage : locals.maximum;
    }
    if (locals.usage_count == 0)
        return 0;
    return locals.usages[index < locals.usage_count ? index : locals.usage_count - 1];
}

ReportField field(uint16_t bit, uint8_t size, bool is_signed) {
    return {true, is_signed, size, bit};
}

}  // namespace

ReportMap parse_report_map(const uint8_t* descriptor, size_t length) {
    ReportMap map{};
    MouseLayout mouse{};
    uint16_t offsets[REPORT_IDS]{};  // Input bits so far, per report ID
    uint16_t page = 0;
    int32_t logical_minimum = 0;
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    uint8_t report_id = 0;
    Locals locals{};
    size_t depth = 0;
    size_t mouse_depth = 0;  // Depth of the open mouse collection, 0 if none
    size_t keyboard_depth = 0;
    for (size_t at = 0; at < length;) {
        const uint8_t prefix = descriptor[at];
        if (prefix == ITEM_LONG) {
            at += at + 1 < length ? 3U + descriptor[at + 1] : length;
            continue;
        }
        const size_t size = (prefix & 3) == 3 ? 4 : (prefix & 3);
        if (at + 1 + size > length)
            break;
        uint32_t data = 0;
        for (size_t i = 0; i < size; ++i)
            data |= static_cast<uint32_t>(descriptor[at + 1 + i]) << (8 * i);
        int32_t signed_data = static_cast<int32_t>(data);
        if (size == 1)
            signed_data = static_cast<int8_t>(data);
        else if (size == 2)
            signed_data = static_cast<int16_t>(data);
        const uint8_t type = (prefix >> 2) & 3;
        const uint8_t tag = prefix >> 4;
        at += 1 + size;

        if (type == TYPE_GLOBAL) {
            if (tag == GLOBAL_USAGE_PAGE)
                page = static_cast<uint16_t>(data);
            else if (tag == GLOBAL_LOGICAL_MINIMUM)
                logical_minimum = signed_data;
            else if (tag == GLOBAL_REPORT_SIZE)
                report_size = data;
            else if (tag == GLOBAL_REPORT_ID)
                report_id = static_cast<uint8_t>(data);
            else if (tag == GLOBAL_REPORT_COUNT)
                report_count = data;
            continue;
        }
        if (type == TYPE_LOCAL) {
            if (tag == LOCAL_USAGE && locals.usage_count < MAX_USAGES)
                locals.usages[locals.usage_count++] = extended(data, size, page);
            else if (tag == LOCAL_USAGE_MINIMUM) {
                locals.minimum = extended(data, size, page);
                locals.range = true;
            } else if (tag == LOCAL_USAGE_MAXIMUM)
                locals.maximum = extended(data, size, page);
            continue;
        }
        if (type != TYPE_MAIN)
            continue;
        if (tag == MAIN_COLLECTION) {
            ++depth;
            const uint32_t usage = usage_at(locals, 0);
            if (data == COLLECTION_APPLICATION && usage >> PAGE_SHIFT == PAGE_GENERIC_DESKTOP) {
                if ((usage & 0xFFFF) == USAGE_MOUSE && mouse_depth == 0)
                    mouse_depth = depth;
                else if ((usage & 0xFFFF) == USAGE_KEYBOARD && keyboard_depth == 0)
                    keyboard_depth = depth;
            }
        } else if (tag == MAIN_END_COLLECTION) {
            if (depth == mouse_depth)
                mouse_depth = 0;
            if (depth == keyboard_depth)
                keyboard_depth = 0;
            depth = depth > 0 ? depth - 1 : 0;
        } else if (tag == MAIN_INPUT) {
            const uint16_t start = offsets[report_id];
            if (keyboard_depth != 0 && !map.keyboard) {
                map.keyboard = true;
                map.keyboard_report_id = report_id;
            }
            if (mouse_depth != 0 && (data & INPUT_CONSTANT) == 0 && !mouse.valid) {
                for (size_t i = 0; i < report_count; ++i) {
                    const uint32_t usage = usage_at(locals, i);
                    const auto bit = static_cast<uint16_t>(start + i * report_size);
                    const auto bits = static_cast<uint8_t>(report_size);
                    const bool is_signed = logical_minimum < 0;
                    if (usage >> PAGE_SHIFT == PAGE_BUTTON && report_size == 1 && !mouse.buttons.present) {
                        const auto buttons = report_count < MAX_BUTTONS ? report_count : MAX_BUTTONS;
                        mouse.buttons = field(bit, static_cast<uint8_t>(buttons), false);
                        mouse.report_id = report_id;
                    } else if (usage >> PAGE_SHIFT == PAGE_GENERIC_DESKTOP) {
                        if ((usage & 0xFFFF) == USAGE_X)
                            mouse.x = field(bit, bits, is_signed);
                        else if ((usage & 0xFFFF) == USAGE_Y)
                            mouse.y = field(bit, bits, is_signed);
                        else if ((usage & 0xFFFF) == USAGE_WHEEL)
                            mouse.wheel = field(bit, bits, is_signed);
                        mouse.report_id = report_id;
                    }
                }
            }
            offsets[report_id] = static_cast<uint16_t>(start + report_size * report_count);
        }
        // Main items consume the local items before them.
        locals = Locals{};
    }
    mouse.valid = mouse.x.present && mouse.y.present;
    if (mouse.valid)
        map.mouse = mouse;
    return map;
}

int32_t field_value(const ReportField& field, const uint8_t* report, size_t length) {
    if (!field.present || field.size == 0 || field.size > 32)
        return 0;
    uint32_t value = 0;
    for (uint8_t i = 0; i < field.size; ++i) {
        const size_t bit = field.bit + i;
        if (bit / 8 >= length)
            break;
        value |= static_cast<uint32_t>((report[bit / 8] >> (bit % 8)) & 1) << i;
    }
    if (field.is_signed && field.size < 32 && (value & (1U << (field.size - 1))) != 0)
        value |= ~((1U << field.size) - 1);
    return static_cast<int32_t>(value);
}

}  // namespace input
