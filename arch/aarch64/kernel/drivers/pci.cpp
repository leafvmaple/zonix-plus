#include "drivers/pci.h"
#include "drivers/pci_ecam.h"

namespace pci {

int init() {
    Error error = EcamConfig::init(0x4010000000ULL, 0x00100000);
    return error == Error::None ? 0 : -1;
}
uint32_t config_read32(int bus, int slot, int function, int offset) {
    return EcamConfig::read32(bus, slot, function, offset);
}
void config_write32(int bus, int slot, int function, int offset, uint32_t value) {
    EcamConfig::write32(bus, slot, function, offset, value);
}
int bus_count() {
    return EcamConfig::bus_count();
}

}  // namespace pci
