#pragma once

#include <kernel/config.h>
#ifdef CONFIG_PS2KBD

#include <base/types.h>

namespace kbd {

void init(void);
int getc(void);

} // namespace kbd

#endif // CONFIG_PS2KBD