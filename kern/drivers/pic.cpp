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
    // Full PIC initialization (ICW1-ICW4)
    // This must be done in the kernel because UEFI does not go through vbr.S
    // which previously handled PIC init for the BIOS path.

    // ICW1: Init both PICs, indicate ICW4 will follow
    outb(PIC1_CMD, ICW1_ICW4 | ICW1_INIT);
    outb(PIC2_CMD, ICW1_ICW4 | ICW1_INIT);

    // ICW2: Set interrupt vector offsets
    outb(PIC1_IMR, IRQ_OFFSET);      // Master: IRQ 0-7  -> INT 0x20-0x27
    outb(PIC2_IMR, IRQ_OFFSET + 8);  // Slave:  IRQ 8-15 -> INT 0x28-0x2F

    // ICW3: Tell Master about Slave on IRQ2, tell Slave its cascade identity
    outb(PIC1_IMR, BIT_SLAVE);
    outb(PIC2_IMR, IRQ_SLAVE);

    // ICW4: 8086 mode, auto EOI
    outb(PIC1_IMR, ICW4_8086 | ICW4_AUTO);
    outb(PIC2_IMR, ICW4_8086 | ICW4_AUTO);

    // Restore current irq_mask to hardware (preserves IRQs already
    // enabled by earlier init code, e.g. kbd::init() before us)
    outb(PIC1_IMR, irq_mask & 0xFF);
    outb(PIC2_IMR, (irq_mask >> 8) & 0xFF);

    // Ensure cascade (slave PIC) is enabled
    enable(IRQ_SLAVE);
}

} // namespace pic
