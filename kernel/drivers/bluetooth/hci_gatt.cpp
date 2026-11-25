// The GATT client side of the HID over GATT profile: finds the keyboard's
// HID service and its input reports, preferring the boot keyboard report,
// whose layout is fixed, and subscribes to their notifications.
#include "drivers/bluetooth/hci.h"
#include "drivers/bluetooth/hci_defs.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include <kernel/log.h>

namespace bluetooth {
namespace {

constexpr uint16_t UUID_PRIMARY_SERVICE = 0x2800;
constexpr uint16_t UUID_CHARACTERISTIC = 0x2803;
constexpr uint16_t UUID_CLIENT_CONFIGURATION = 0x2902;
constexpr uint16_t UUID_REPORT_REFERENCE = 0x2908;
constexpr uint16_t UUID_HID_SERVICE = 0x1812;
constexpr uint16_t UUID_BOOT_KEYBOARD_INPUT = 0x2A22;
constexpr uint16_t UUID_REPORT = 0x2A4D;
constexpr uint16_t UUID_PROTOCOL_MODE = 0x2A4E;
constexpr uint16_t LAST_HANDLE = 0xFFFF;
constexpr uint8_t PROPERTY_NOTIFY = 0x10;
constexpr uint8_t REPORT_TYPE_INPUT = 1;
constexpr uint8_t BOOT_PROTOCOL = 0;
constexpr uint8_t FORMAT_16_BIT = 1;
constexpr uint64_t ATT_TIMEOUT_MS = 10000;
constexpr size_t MAX_CHARACTERISTICS = 16;

struct Characteristic {
    uint16_t declaration{};
    uint16_t value{};
    uint8_t properties{};
    uint16_t uuid{};  // 0 for 128-bit UUIDs, which the HID service does not use
};

}  // namespace

// One ATT request and its response, which may be an error response.
Error Hci::attribute_request(Controller& controller, const uint8_t* request, size_t length, Frame& response) {
    TRY(send_frame(controller, CHANNEL_ATT, request, length));
    TRY(wait_frame(controller, CHANNEL_ATT, ATT_TIMEOUT_MS, response));
    ENSURE(response.length != 0, Error::Io);
    return Error::None;
}

Error Hci::find_keyboard_reports(Controller& controller) {
    Frame response{};
    // The HID service among the primary services.
    uint16_t service_start = 0;
    uint16_t service_end = 0;
    for (uint32_t start = 1; start <= LAST_HANDLE;) {
        uint8_t request[7] = {ATT_READ_BY_GROUP_REQUEST};
        write16(request + 1, static_cast<uint16_t>(start));
        write16(request + 3, LAST_HANDLE);
        write16(request + 5, UUID_PRIMARY_SERVICE);
        TRY(attribute_request(controller, request, sizeof(request), response));
        if (response.data[0] != ATT_READ_BY_GROUP_RESPONSE || response.length < 2 || response.data[1] < 6)
            break;
        // Entries: start handle, end handle, service UUID.
        const size_t entry = response.data[1];
        uint32_t next = 0;
        for (size_t at = 2; at + entry <= response.length; at += entry) {
            if (entry == 6 && read16(response.data + at + 4) == UUID_HID_SERVICE) {
                service_start = read16(response.data + at);
                service_end = read16(response.data + at + 2);
            }
            next = read16(response.data + at + 2) + 1U;
        }
        if (next <= start)
            break;
        start = next;
    }
    ENSURE_LOG(service_start != 0, Error::NotFound, "bluetooth hci%u: the device has no HID service", controller.index);

    // Its characteristics.
    Characteristic characteristics[MAX_CHARACTERISTICS];
    size_t count = 0;
    for (uint32_t start = service_start; start <= service_end && count < MAX_CHARACTERISTICS;) {
        uint8_t request[7] = {ATT_READ_BY_TYPE_REQUEST};
        write16(request + 1, static_cast<uint16_t>(start));
        write16(request + 3, service_end);
        write16(request + 5, UUID_CHARACTERISTIC);
        TRY(attribute_request(controller, request, sizeof(request), response));
        if (response.data[0] != ATT_READ_BY_TYPE_RESPONSE || response.length < 2 || response.data[1] < 7)
            break;
        // Entries: declaration handle, properties, value handle, UUID.
        const size_t entry = response.data[1];
        uint32_t next = 0;
        for (size_t at = 2; at + entry <= response.length && count < MAX_CHARACTERISTICS; at += entry) {
            Characteristic& found = characteristics[count++];
            found.declaration = read16(response.data + at);
            found.properties = response.data[at + 2];
            found.value = read16(response.data + at + 3);
            found.uuid = entry == 7 ? read16(response.data + at + 5) : 0;
            next = found.declaration + 1U;
        }
        if (next <= start)
            break;
        start = next;
    }

    // A characteristic's descriptors lie between its value and the next
    // declaration.
    auto find_descriptor = [&](size_t index, uint16_t uuid, uint16_t& handle) -> Error {
        const uint32_t last = index + 1 < count ? characteristics[index + 1].declaration - 1U : service_end;
        for (uint32_t start = characteristics[index].value + 1U; start <= last;) {
            uint8_t request[5] = {ATT_FIND_INFORMATION_REQUEST};
            write16(request + 1, static_cast<uint16_t>(start));
            write16(request + 3, static_cast<uint16_t>(last));
            TRY(attribute_request(controller, request, sizeof(request), response));
            if (response.data[0] != ATT_FIND_INFORMATION_RESPONSE || response.length < 2 ||
                response.data[1] != FORMAT_16_BIT)
                break;
            uint32_t next = 0;
            for (size_t at = 2; at + 4 <= response.length; at += 4) {
                if (read16(response.data + at + 2) == uuid) {
                    handle = read16(response.data + at);
                    return Error::None;
                }
                next = read16(response.data + at) + 1U;
            }
            if (next <= start)
                break;
            start = next;
        }
        return Error::NotFound;
    };

    Bond reports{};
    // Boot protocol: the boot keyboard report has the USB boot layout.
    size_t boot = count;
    size_t mode = count;
    for (size_t i = 0; i < count; ++i) {
        if (characteristics[i].uuid == UUID_BOOT_KEYBOARD_INPUT &&
            (characteristics[i].properties & PROPERTY_NOTIFY) != 0)
            boot = i;
        else if (characteristics[i].uuid == UUID_PROTOCOL_MODE)
            mode = i;
    }
    uint16_t config = 0;
    if (boot < count && mode < count && find_descriptor(boot, UUID_CLIENT_CONFIGURATION, config) == Error::None) {
        reports.protocol_mode = characteristics[mode].value;
        reports.reports[0] = characteristics[boot].value;
        reports.configs[0] = config;
        reports.report_count = 1;
    }
    // Report protocol otherwise: every input report, of which those in the
    // boot layout are keyboard reports.
    for (size_t i = 0; i < count && reports.report_count == 0; ++i) {
        if (characteristics[i].uuid != UUID_REPORT || (characteristics[i].properties & PROPERTY_NOTIFY) == 0)
            continue;
        uint16_t reference = 0;
        if (find_descriptor(i, UUID_REPORT_REFERENCE, reference) == Error::None) {
            uint8_t request[3] = {ATT_READ_REQUEST};
            write16(request + 1, reference);
            // Report ID, report type.
            TRY(attribute_request(controller, request, sizeof(request), response));
            if (response.data[0] != ATT_READ_RESPONSE || response.length < 3 || response.data[2] != REPORT_TYPE_INPUT)
                continue;
        }
        if (find_descriptor(i, UUID_CLIENT_CONFIGURATION, config) != Error::None || reports.report_count == MAX_REPORTS)
            continue;
        reports.reports[reports.report_count] = characteristics[i].value;
        reports.configs[reports.report_count++] = config;
    }
    intr::Guard guard;
    if (reports.report_count == 0) {
        controller.bond = Bond{};  // Nothing to come back for
        klog::warn("bluetooth hci%u: the HID service has no input report to use\n", controller.index);
        return Error::NotFound;
    }
    Bond& bond = controller.bond;
    bond.protocol_mode = reports.protocol_mode;
    memcpy(bond.reports, reports.reports, sizeof(bond.reports));
    memcpy(bond.configs, reports.configs, sizeof(bond.configs));
    bond.report_count = reports.report_count;
    klog::info("bluetooth hci%u: HID service at 0x%04x-0x%04x, %s protocol\n", controller.index, service_start,
               service_end, reports.protocol_mode != 0 ? "boot" : "report");
    return Error::None;
}

// Puts the keyboard in boot protocol when it has it and turns its reports'
// notifications on, after which they arrive as key events.
Error Hci::subscribe(Controller& controller) {
    Bond bond{};
    {
        intr::Guard guard;
        bond = controller.bond;
        ENSURE(bond.report_count != 0, Error::NotFound);
        memcpy(controller.link.reports, bond.reports, sizeof(bond.reports));
        controller.link.report_count = bond.report_count;
    }
    if (bond.protocol_mode != 0) {
        uint8_t command[4] = {ATT_WRITE_COMMAND};
        write16(command + 1, bond.protocol_mode);
        command[3] = BOOT_PROTOCOL;
        TRY(send_frame(controller, CHANNEL_ATT, command, sizeof(command)));
    }
    Frame response{};
    for (size_t i = 0; i < bond.report_count; ++i) {
        uint8_t request[5] = {ATT_WRITE_REQUEST};
        write16(request + 1, bond.configs[i]);
        write16(request + 3, 1);  // Notifications on
        TRY(attribute_request(controller, request, sizeof(request), response));
        ENSURE_LOG(response.data[0] == ATT_WRITE_RESPONSE, Error::Io,
                   "bluetooth hci%u: the keyboard refused notifications (error 0x%02x)", controller.index,
                   response.length >= 5 ? response.data[4] : 0);
    }
    klog::info("bluetooth hci%u: keyboard ready\n", controller.index);
    return Error::None;
}

}  // namespace bluetooth
