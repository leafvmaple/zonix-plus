#include "drivers/pci.h"
#include "lib/stdio.h"

namespace {

constexpr int MAX_ENUM_DEVICES = 512;
constexpr int MAX_REGISTERED_DRIVERS = 32;

pci::DeviceInfo s_devices[MAX_ENUM_DEVICES];
int s_device_count = 0;
bool s_scanned = false;

const pci::Driver* s_drivers[MAX_REGISTERED_DRIVERS];
int s_driver_count = 0;

int s_bound_driver[MAX_ENUM_DEVICES];

bool is_present(uint32_t id) {
    return !(id == 0xFFFFFFFF || (id & 0xFFFF) == 0xFFFF);
}

uint8_t read_header_type(int bus, int dev, int func) {
    uint32_t h = pci::config_read32(bus, dev, func, pci::HEADER_TYPE);
    return static_cast<uint8_t>((h >> 16) & 0xFF);
}

void reset_bindings() {
    for (int i = 0; i < MAX_ENUM_DEVICES; i++)
        s_bound_driver[i] = -1;
}

void scan_all_devices() {
    s_device_count = 0;

    int buses = pci::bus_count();
    for (int bus = 0; bus < buses; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id0 = pci::config_read32(bus, dev, 0, pci::VENDOR_ID);
            if (!is_present(id0)) {
                continue;
            }

            uint8_t hdr0 = read_header_type(bus, dev, 0);
            int funcs = (hdr0 & 0x80) ? 8 : 1;

            for (int func = 0; func < funcs; func++) {
                uint32_t id = (func == 0) ? id0 : pci::config_read32(bus, dev, func, pci::VENDOR_ID);
                if (!is_present(id)) {
                    continue;
                }

                if (s_device_count >= MAX_ENUM_DEVICES) {
                    cprintf("pci: device table full, max=%d\n", MAX_ENUM_DEVICES);
                    s_scanned = true;
                    reset_bindings();
                    return;
                }

                uint32_t cr = pci::config_read32(bus, dev, func, pci::CLASS_REVISION);
                uint8_t hdr = (func == 0) ? hdr0 : read_header_type(bus, dev, func);

                pci::DeviceInfo& di = s_devices[s_device_count++];
                di.bus = static_cast<uint8_t>(bus);
                di.dev = static_cast<uint8_t>(dev);
                di.func = static_cast<uint8_t>(func);
                di.vendor = static_cast<uint16_t>(id & 0xFFFF);
                di.device = static_cast<uint16_t>(id >> 16);
                di.cls = static_cast<uint8_t>((cr >> 24) & 0xFF);
                di.subcls = static_cast<uint8_t>((cr >> 16) & 0xFF);
                di.iface = static_cast<uint8_t>((cr >> 8) & 0xFF);
                di.header_type = static_cast<uint8_t>(hdr & 0x7F);
            }
        }
    }

    s_scanned = true;
    reset_bindings();
}

void ensure_scanned() {
    if (!s_scanned) {
        scan_all_devices();
    }
}

bool id_matches(const pci::DriverId& id, const pci::DeviceInfo& dev) {
    if (id.vendor != pci::ANY_ID && id.vendor != dev.vendor)
        return false;
    if (id.device != pci::ANY_ID && id.device != dev.device)
        return false;
    if (id.cls != pci::ANY_CLASS && id.cls != dev.cls)
        return false;
    if (id.subcls != pci::ANY_CLASS && id.subcls != dev.subcls)
        return false;
    if (id.iface != pci::ANY_CLASS && id.iface != dev.iface)
        return false;
    return true;
}

const pci::DriverId* find_matching_id(const pci::Driver* driver, const pci::DeviceInfo& dev) {
    for (int i = 0; i < driver->id_count; i++) {
        if (id_matches(driver->id_table[i], dev)) {
            return &driver->id_table[i];
        }
    }
    return nullptr;
}

}  // namespace

namespace pci {

int register_driver(const Driver* driver) {
    if (driver == nullptr || driver->probe == nullptr || driver->id_table == nullptr || driver->id_count <= 0) {
        return -1;
    }

    for (int i = 0; i < s_driver_count; i++) {
        if (s_drivers[i] == driver) {
            return 0;
        }
    }

    if (s_driver_count >= MAX_REGISTERED_DRIVERS) {
        cprintf("pci: driver table full, max=%d\n", MAX_REGISTERED_DRIVERS);
        return -1;
    }

    s_drivers[s_driver_count++] = driver;
    return 0;
}

int probe_drivers() {
    ensure_scanned();

    for (int i = 0; i < s_device_count; i++) {
        if (s_bound_driver[i] >= 0) {
            continue;
        }

        for (int d = 0; d < s_driver_count; d++) {
            const Driver* drv = s_drivers[d];
            const DriverId* matched = find_matching_id(drv, s_devices[i]);
            if (matched == nullptr) {
                continue;
            }

            int rc = drv->probe(&s_devices[i], matched);
            if (rc == 0) {
                s_bound_driver[i] = d;
                break;
            }
        }
    }

    return 0;
}

uint32_t read_bar(int bus, int dev, int func, int bar_index) {
    if (bar_index < 0 || bar_index > 5)
        return 0;
    return config_read32(bus, dev, func, BAR0 + bar_index * 4);
}

void enable_bus_master(int bus, int dev, int func) {
    uint32_t cmd = config_read32(bus, dev, func, COMMAND);
    cmd |= CMD_BUS_MASTER | CMD_MEMORY_SPACE;
    config_write32(bus, dev, func, COMMAND, cmd);
}


}  // namespace pci
