#include "pci.h"
#include <asm/arch.h>
#include "lib/stdio.h"

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

uint32_t PCILocation::read_bar(uint8_t bar_index) {
    if (bar_index > 5) {
        return 0;
    }
    
    uint8_t offset = 0x10 + (bar_index * 4);
    return read_config32(offset);
}

bool PCILocation::find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t interface, PCILocation* loc) {
    uint32_t expected_class = (class_code << 16) | (subclass << 8) | interface;

    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            for (int func = 0; func < 8; func++) {
                PCILocation pci_loc(bus, device, func);
                uint32_t vendor = pci_loc.read_config32(pci::PCI_VENDOR_ID);

                if (vendor == 0xFFFFFFFF) {
                    continue;
                }
                
                uint32_t class_reg = pci_loc.read_config32(pci::PCI_CLASS_REVISION);

                if ((class_reg >> 8) == expected_class) {
                    *loc = pci_loc;
                    return true;
                }
            }
        }
    }
    
    return false;
}

void PCILocation::enable_bus_master() {
    uint32_t cmd = read_config32(pci::PCI_COMMAND);
    cmd |= pci::CMD_BUS_MASTER | pci::CMD_MEMORY_SPACE;
    write_config32(pci::PCI_COMMAND, cmd);
}
