#include "drivers/pci.h"
#include "lib/array.h"
#include "lib/stdio.h"

namespace {

constexpr int MAX_ENUM_DEVICES = 512;
constexpr int MAX_REGISTERED_DRIVERS = 32;

Array<pci::DeviceInfo, MAX_ENUM_DEVICES> enumerated_devices{};
bool scan_complete{};

Array<const pci::Driver*, MAX_REGISTERED_DRIVERS> registered_drivers{};

Array<int, MAX_ENUM_DEVICES> bound_driver_indexes{};

bool is_present(uint32_t id) {
    return !(id == 0xFFFFFFFF || (id & 0xFFFF) == 0xFFFF);
}

uint8_t read_header_type(int bus, int slot, int function) {
    uint32_t h = pci::config_read32(bus, slot, function, pci::HeaderType);
    return static_cast<uint8_t>((h >> 16) & 0xFF);
}

void reset_bindings() {
    bound_driver_indexes.fill(-1);
}

void scan_all_devices() {
    enumerated_devices.clear();

    int buses = pci::bus_count();
    for (int bus = 0; bus < buses; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id0 = pci::config_read32(bus, slot, 0, pci::VendorId);
            if (!is_present(id0)) {
                continue;
            }

            uint8_t hdr0 = read_header_type(bus, slot, 0);
            int funcs = (hdr0 & 0x80) ? 8 : 1;

            for (int function = 0; function < funcs; function++) {
                uint32_t id = (function == 0) ? id0 : pci::config_read32(bus, slot, function, pci::VendorId);
                if (!is_present(id)) {
                    continue;
                }

                if (enumerated_devices.full()) {
                    cprintf("pci: device table full, max=%d\n", MAX_ENUM_DEVICES);
                    scan_complete = true;
                    reset_bindings();
                    return;
                }

                uint32_t cr = pci::config_read32(bus, slot, function, pci::ClassRevision);
                uint8_t hdr = (function == 0) ? hdr0 : read_header_type(bus, slot, function);

                pci::DeviceInfo di{};
                di.bus = static_cast<uint8_t>(bus);
                di.slot = static_cast<uint8_t>(slot);
                di.function = static_cast<uint8_t>(function);
                di.vendor_id = static_cast<uint16_t>(id & 0xFFFF);
                di.device_id = static_cast<uint16_t>(id >> 16);
                di.class_code = static_cast<uint8_t>((cr >> 24) & 0xFF);
                di.subclass = static_cast<uint8_t>((cr >> 16) & 0xFF);
                di.prog_if = static_cast<uint8_t>((cr >> 8) & 0xFF);
                di.header_type = static_cast<uint8_t>(hdr & 0x7F);
                enumerated_devices.push_back(di);
            }
        }
    }

    scan_complete = true;
    reset_bindings();
}

void ensure_scanned() {
    if (!scan_complete) {
        scan_all_devices();
    }
}

bool id_matches(const pci::DriverId& id, const pci::DeviceInfo& device_info) {
    if (id.vendor_id != pci::ANY_ID && id.vendor_id != device_info.vendor_id)
        return false;
    if (id.device_id != pci::ANY_ID && id.device_id != device_info.device_id)
        return false;
    if (id.class_code != pci::ANY_CLASS && id.class_code != device_info.class_code)
        return false;
    if (id.subclass != pci::ANY_CLASS && id.subclass != device_info.subclass)
        return false;
    if (id.prog_if != pci::ANY_CLASS && id.prog_if != device_info.prog_if)
        return false;
    return true;
}

const pci::DriverId* find_matching_id(const pci::Driver* driver, const pci::DeviceInfo& device_info) {
    for (int i = 0; i < driver->id_count; i++) {
        if (id_matches(driver->id_table[i], device_info)) {
            return &driver->id_table[i];
        }
    }
    return nullptr;
}

}  // namespace

namespace pci {

Error register_driver(const Driver* driver) {
    ENSURE(driver && driver->probe && driver->id_table && driver->id_count > 0, Error::Invalid);

    for (const pci::Driver* d : registered_drivers) {
        if (d == driver) {
            return Error::None;
        }
    }

    ENSURE_LOG(registered_drivers.push_back(driver), Error::Full, "pci: driver table full, max=%d",
               MAX_REGISTERED_DRIVERS);

    return Error::None;
}

int probe_drivers() {
    ensure_scanned();

    for (size_t i = 0; i < enumerated_devices.size(); i++) {
        if (bound_driver_indexes[i] >= 0) {
            continue;
        }

        for (size_t d = 0; d < registered_drivers.size(); d++) {
            const Driver* drv = registered_drivers[d];
            const DriverId* matched = find_matching_id(drv, enumerated_devices[i]);
            if (matched == nullptr) {
                continue;
            }

            Error rc = drv->probe(&enumerated_devices[i], matched);
            if (rc == Error::None) {
                bound_driver_indexes[i] = static_cast<int>(d);
                break;
            }
        }
    }

    return 0;
}

uint32_t read_bar(int bus, int slot, int function, int bar_index) {
    if (bar_index < 0 || bar_index > 5)
        return 0;
    return config_read32(bus, slot, function, Bar0 + bar_index * 4);
}

void enable_bus_master(int bus, int slot, int function) {
    uint32_t cmd = config_read32(bus, slot, function, Command);
    cmd |= CMD_BUS_MASTER | CMD_MEMORY_SPACE;
    config_write32(bus, slot, function, Command, cmd);
}


}  // namespace pci
