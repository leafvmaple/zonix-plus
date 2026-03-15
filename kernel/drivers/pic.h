#pragma once

#include <base/types.h>

namespace pic {

int init();

void setmask(uint16_t mask);
void enable(unsigned int irq);
void send_eoi(unsigned int irq);

}  // namespace pic
