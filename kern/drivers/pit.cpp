#include "pit.h"
#include "pic.h"

#include <arch/x86/io.h>
#include <arch/x86/drivers/i8254.h>
#include <arch/x86/drivers/i8259.h>

volatile int64_t ticks = 0;

#define TIMER_FREQ 1193180
#define TIMER_DIV(x) (TIMER_FREQ / (x))

static uint8_t CMOS_READ(uint8_t addr) {
	outb(0x70, 0x80 | addr);
	return inb(0x71);
}

#define BCD_TO_BIN(val) (((val) & 0xF) + ((val) >> 4) * 10)

void pit_init(void) {
    struct tm time;

    do {
		time.tm_sec  = CMOS_READ(0);
		time.tm_min  = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon  = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));

    time.tm_sec  = BCD_TO_BIN(time.tm_sec);
	time.tm_min  = BCD_TO_BIN(time.tm_min);
	time.tm_hour = BCD_TO_BIN(time.tm_hour);
	time.tm_mday = BCD_TO_BIN(time.tm_mday);
	time.tm_mon  = BCD_TO_BIN(time.tm_mon);
	time.tm_year = BCD_TO_BIN(time.tm_year);

    outb(PIT_CTRL_REG, PIT_SEL_TIMER0 | PIT_RATE_GEN | PIT_16BIT);

    outb(PIT_TIMER0_REG, TIMER_DIV(100) % 256);
    outb(PIT_TIMER0_REG, TIMER_DIV(100) / 256);

    pic_enable(IRQ_TIMER);
}