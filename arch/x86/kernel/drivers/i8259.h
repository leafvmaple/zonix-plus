#pragma once

#include <base/types.h>

namespace i8259 {

int init();

void setmask(uint16_t mask);
void enable(unsigned int irq);
void send_eoi(unsigned int irq);

}  // namespace i8259
