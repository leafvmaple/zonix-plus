#pragma once

#define PIT_TIMER0_REG 0x40
#define PIT_TIMER1_REG 0x41
#define PIT_TIMER2_REG 0x42

#define PIT_CTRL_REG   0x43

#define PIT_SEL_TIMER0  0x00
#define PIT_SEL_TIMER1  0x40
#define PIT_SEL_TIMER2  0x80

#define PIT_BINARY 0x00  // 二进制计数器
#define PIT_BCD    0x01  // BCD（Binary-Coded Decimal）计数器

#define PIT_RATE_GEN  0x04

#define PIT_16BIT     0x30