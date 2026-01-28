#pragma once

// I8259A PIC

#define PIC1_CMD 0x20  // Master (IRQs 0-7)
#define PIC1_IMR 0x21

#define PIC2_CMD 0xA0   // Slave  (IRQs 8-15)
#define PIC2_IMR 0xA1

#define ICW1_ICW4       0x01		/* Indicates that ICW4 will be present */
#define ICW1_SINGLE     0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04		/* Call address interval 4 (8) */
#define ICW1_LEVEL      0x08		/* Level triggered (edge) mode */
#define ICW1_INIT       0x10		/* Initialization - required! */

#define ICW4_8086       0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO       0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE  0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C		/* Buffered mode/master */
#define ICW4_SFNM       0x10		/* Special fully nested (not) */

#define OCW3_READ_IRR   0x02		/* Read interrupt request */
#define OCW3_READ_ISR   0x03		/* Read interrupt status */
#define OCW3_POLL       0x04		/* Poll Mode */
#define OCW3_CLEAR_MASK 0x40		/* Clear mask */
#define OCW3_SET_MASK   0x60		/* Set mask */

#define OCW3_ASM(n)     (0x08 | (n))

#define T_PGFLT 14  // page fault

#define IRQ_OFFSET 0x20

#define IRQ_TIMER 0
#define IRQ_KBD   1
#define IRQ_SLAVE 2

#define IRQ_RTC   8
#define IRQ_IDE1  14
#define IRQ_IDE2  15

#define BIT_SLAVE (1 << IRQ_SLAVE)