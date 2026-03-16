#pragma once

#include <kernel/config.h>
#include <base/types.h>

namespace i8042 {

void init(void);
int getc(void);
void intr(void);
char getc_blocking(void);

}  // namespace i8042
