#pragma once

#include <base/types.h>

namespace gic {

int init();
void enable(uint32_t intid);
void send_eoi(uint32_t iar);
uint32_t ack();  // read GICC_IAR, returns full IAR value

}  // namespace gic
