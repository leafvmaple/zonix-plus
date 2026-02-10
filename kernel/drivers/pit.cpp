#include "pit.h"
#include "pic.h"

#include <asm/io.h>
#include <asm/drivers/i8254.h>
#include <asm/drivers/i8259.h>

namespace {

constexpr uint32_t TIMER_FREQ = 1193180;

constexpr uint32_t timer_div(uint32_t x) {
    return TIMER_FREQ / x;
}

uint8_t cmos_read(uint8_t addr) {
    outb(0x70, 0x80 | addr);
    return inb(0x71);
}

constexpr uint8_t bcd_to_bin(uint8_t val) {
    return (val & 0xF) + (val >> 4) * 10;
}

} // namespace

namespace pit {

volatile int64_t ticks = 0;

void init() {
    struct tm time;

    do {
        time.tm_sec  = cmos_read(0);
        time.tm_min  = cmos_read(2);
        time.tm_hour = cmos_read(4);
        time.tm_mday = cmos_read(7);
        time.tm_mon  = cmos_read(8);
        time.tm_year = cmos_read(9);
    } while (time.tm_sec != cmos_read(0));

    time.tm_sec  = bcd_to_bin(static_cast<uint8_t>(time.tm_sec));
    time.tm_min  = bcd_to_bin(static_cast<uint8_t>(time.tm_min));
    time.tm_hour = bcd_to_bin(static_cast<uint8_t>(time.tm_hour));
    time.tm_mday = bcd_to_bin(static_cast<uint8_t>(time.tm_mday));
    time.tm_mon  = bcd_to_bin(static_cast<uint8_t>(time.tm_mon));
    time.tm_year = bcd_to_bin(static_cast<uint8_t>(time.tm_year));

    outb(PIT_CTRL_REG, PIT_SEL_TIMER0 | PIT_RATE_GEN | PIT_16BIT);

    outb(PIT_TIMER0_REG, timer_div(100) % 256);
    outb(PIT_TIMER0_REG, timer_div(100) / 256);

    pic::enable(IRQ_TIMER);
}

} // namespace pit