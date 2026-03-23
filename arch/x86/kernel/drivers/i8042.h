#pragma once

#include <kernel/config.h>
#include <base/types.h>

namespace i8042 {

int init();
int getc();
void intr();
char getc_blocking();

}  // namespace i8042
