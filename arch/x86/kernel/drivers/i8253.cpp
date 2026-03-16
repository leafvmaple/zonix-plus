#include "drivers/i8253.h"
#include "drivers/i8259.h"
#include "lib/stdio.h"

#include <asm/arch.h>
#include <asm/drivers/i8254.h>
#include <asm/drivers/i8259.h>

namespace {

constexpr uint32_t TIMER_FREQ = 1193180;

constexpr uint32_t timer_div(uint32_t x) {
    return TIMER_FREQ / x;
}

uint8_t cmos_read(uint8_t addr) {
    arch_port_outb(0x70, 0x80 | addr);
    return arch_port_inb(0x71);
}

constexpr uint8_t bcd_to_bin(uint8_t val) {
    return (val & 0xF) + (val >> 4) * 10;
}

}  // namespace

namespace timer {
volatile int64_t ticks = 0;
}  // namespace timer

namespace i8253 {

int init() {
    struct tm time;

    int retries = 0;
    constexpr int MAX_CMOS_RETRIES = 1000;
    do {
        time.tm_sec = cmos_read(0);
        time.tm_min = cmos_read(2);
        time.tm_hour = cmos_read(4);
        time.tm_mday = cmos_read(7);
        time.tm_mon = cmos_read(8);
        time.tm_year = cmos_read(9);
        if (++retries > MAX_CMOS_RETRIES) {
            cprintf("i8253: CMOS read unstable after %d retries\n", MAX_CMOS_RETRIES);
            return -1;
        }
    } while (time.tm_sec != cmos_read(0));

    time.tm_sec = bcd_to_bin(static_cast<uint8_t>(time.tm_sec));
    time.tm_min = bcd_to_bin(static_cast<uint8_t>(time.tm_min));
    time.tm_hour = bcd_to_bin(static_cast<uint8_t>(time.tm_hour));
    time.tm_mday = bcd_to_bin(static_cast<uint8_t>(time.tm_mday));
    time.tm_mon = bcd_to_bin(static_cast<uint8_t>(time.tm_mon));
    time.tm_year = bcd_to_bin(static_cast<uint8_t>(time.tm_year));

    arch_port_outb(PIT_CTRL_REG, PIT_SEL_TIMER0 | PIT_RATE_GEN | PIT_16BIT);

    arch_port_outb(PIT_TIMER0_REG, timer_div(100) % 256);
    arch_port_outb(PIT_TIMER0_REG, timer_div(100) / 256);

    i8259::enable(IRQ_TIMER);

    return ARCH_INIT_OK;
}

}  // namespace i8253