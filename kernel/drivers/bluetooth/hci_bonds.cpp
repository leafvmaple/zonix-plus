// Keeps bonds in a file on the root file system, so that paired keyboards
// and mice come back after a restart without pairing again.
#include "drivers/bluetooth/bond_store.h"
#include "drivers/bluetooth/hci.h"
#include "drivers/bluetooth/hci_defs.h"
#include "drivers/intr.h"
#include "fs/vfs.h"
#include "lib/memory.h"
#include <abi/fcntl.h>
#include <kernel/log.h>

namespace bluetooth {
namespace {

constexpr const char* BOND_DIRECTORIES[] = {"/var", "/var/lib", "/var/lib/bluetooth"};
constexpr const char* BOND_FILE = "/var/lib/bluetooth/bonds";

// The records the file holds now; none when it is missing or damaged.
size_t read_records(bond_store::Record* records, bool& damaged) {
    damaged = false;
    auto file = vfs::open(BOND_FILE, O_RDONLY);
    if (!file.ok())
        return 0;
    uint8_t data[bond_store::FILE_CAPACITY];
    auto length = vfs::read(file.value().get(), data, sizeof(data), 0);
    size_t count = 0;
    if (!length.ok() ||
        !bond_store::decode(data, static_cast<size_t>(length.value()), records, bond_store::MAX_RECORDS, count)) {
        damaged = true;
        return 0;
    }
    return count;
}

}  // namespace

// Brings back the devices bonded with this controller, which then reconnect
// when they advertise.
void Hci::load_bonds(Controller& controller) {
    if (!vfs::is_mounted("/"))
        return;
    bond_store::Record records[bond_store::MAX_RECORDS];
    bool damaged = false;
    const size_t count = read_records(records, damaged);
    if (damaged)
        klog::warn("bluetooth hci%u: %s is damaged; its devices must pair again\n", controller.index, BOND_FILE);
    size_t loaded = 0;
    intr::Guard guard;
    for (size_t i = 0; i < count; ++i) {
        if (memcmp(records[i].local, controller.address, ADDRESS_BYTES) != 0 ||
            known(controller, records[i].bond.address))
            continue;
        for (Device& device : controller.devices) {
            if (device.used)
                continue;
            device = Device{};
            device.used = true;
            device.appearance = records[i].appearance;
            device.bond = records[i].bond;
            ++loaded;
            break;
        }
    }
    if (loaded != 0)
        klog::info("bluetooth hci%u: %u bonded devices\n", controller.index, static_cast<unsigned int>(loaded));
}

// Writes this controller's usable bonds, keeping those of other adapters.
void Hci::save_bonds(Controller& controller) {
    if (!vfs::is_mounted("/"))
        return;
    bond_store::Record records[bond_store::MAX_RECORDS];
    bool damaged = false;
    size_t count = 0;
    const size_t stored = read_records(records, damaged);
    for (size_t i = 0; i < stored; ++i)
        if (memcmp(records[i].local, controller.address, ADDRESS_BYTES) != 0)
            records[count++] = records[i];
    {
        intr::Guard guard;
        for (const Device& device : controller.devices) {
            if (!device.used || !device.bond.valid || device.bond.report_count == 0 || count == bond_store::MAX_RECORDS)
                continue;
            bond_store::Record& record = records[count++];
            record = bond_store::Record{};
            memcpy(record.local, controller.address, ADDRESS_BYTES);
            record.appearance = device.appearance;
            record.bond = device.bond;
        }
    }
    uint8_t data[bond_store::FILE_CAPACITY];
    const size_t length = bond_store::encode(records, count, data, sizeof(data));
    for (const char* directory : BOND_DIRECTORIES)
        static_cast<void>(vfs::mkdir(directory));  // Usually there already
    auto file = vfs::open(BOND_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    Error error = file.ok() ? Error::None : file.error();
    if (error == Error::None) {
        auto written = vfs::write(file.value().get(), data, length, 0);
        error = !written.ok() ? written.error()
                              : (static_cast<size_t>(written.value()) == length ? Error::None : Error::Io);
    }
    if (error == Error::None)
        error = vfs::sync();
    if (error != Error::None)
        klog::warn("bluetooth hci%u: cannot save bonds (%d)\n", controller.index, static_cast<int>(error));
}

}  // namespace bluetooth
