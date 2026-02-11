#include "pci.h"
#include <asm/arch.h>
#include "stdio.h"

uint32_t PCILocation::read_config32(uint8_t offset) {
    uint32_t address = get_address() | (offset & 0xFC);
    arch_port_outl(pci::CONFIG_ADDRESS, address);
    return arch_port_inl(pci::CONFIG_DATA);
}

void PCILocation::write_config32(uint8_t offset, uint32_t value) {
    uint32_t address = get_address() | (offset & 0xFC);
    arch_port_outl(pci::CONFIG_ADDRESS, address);
    arch_port_outl(pci::CONFIG_DATA, value);
}

uint32_t PCILocation::read_bar(uint8_t barIndex) {
    if (barIndex > 5) {
        return 0;
    }
    
    uint8_t offset = 0x10 + (barIndex * 4);
    return read_config32(offset);
}

bool PCILocation::find_device_by_class(uint8_t classCode, uint8_t subclass, uint8_t interface, PCILocation* loc) {
    uint32_t expectedClass = (classCode << 16) | (subclass << 8) | interface;

    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            for (int func = 0; func < 8; func++) {
                PCILocation pciLoc(bus, device, func);
                uint32_t vendor = pciLoc.read_config32(0x00);

                if (vendor == 0xFFFFFFFF) {
                    continue;
                }
                
                uint32_t classReg = pciLoc.read_config32(0x08);

                if ((classReg >> 8) == expectedClass) {
                    *loc = pciLoc;
                    return true;
                }
            }
        }
    }
    
    return false;
}

void PCILocation::enable_bus_master() {
    uint32_t cmd = read_config32(0x04);
    cmd |= pci::CMD_BUS_MASTER | pci::CMD_MEMORY_SPACE;
    write_config32(0x04, cmd);
}
