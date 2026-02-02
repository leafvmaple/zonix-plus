#include "pic.h"

#include <arch/x86/io.h>
#include <arch/x86/drivers/i8259.h>

namespace pic {
// Initial IRQ mask has interrupt 2 enabled (for slave 8259A).
static uint16_t irq_mask = 0xFFFF;

void setmask(uint16_t mask) {
    irq_mask = mask;
    outb(PIC1_IMR, mask);
    outb(PIC2_IMR, mask >> 8);
}

void enable(unsigned int irq) {
    setmask(irq_mask & ~(1 << irq));
}

void send_eoi(unsigned int irq) {
    // If this interrupt involved the slave (IRQ 8-15), send EOI to slave
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    // Always send EOI to master
    outb(PIC1_CMD, 0x20);
}

void init(void) {
    // ICW init in vbr.S
    enable(IRQ_SLAVE);
}

} // namespace pic
