#pragma once

#include "lib/result.h"

struct Task;

namespace cons {

int init();
int late_init();
char getc();
Result<char> read_input();
void putc(int c);
void push_input(char c);  // called by input ISRs (pl011, virtio_kbd, ...)
// Called with interrupts disabled at task publication / exit.
Error handoff(Task* parent, Task* child);
void task_exited(Task* task);

// Hardware backends selected by the build; buffering and framebuffer are shared.
int arch_init();
void arch_putc(int c);

}  // namespace cons
