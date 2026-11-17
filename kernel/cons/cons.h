#pragma once

#include "lib/result.h"
#include <abi/terminal.h>

struct Task;

namespace cons {

int init();
int late_init();
char getc();
Result<char> read_input();
void putc(int c);
void write_output(int c);
void push_key(int key);
Error set_raw(bool enabled);
void emergency_restore();
TerminalSize screen_size();
void push_input(char c);  // called by input ISRs (pl011, virtio_kbd, ...)
// Called with interrupts disabled at task publication / exit.
Error handoff(Task* parent, Task* child);
void task_exited(Task* task);

// Hardware backends selected by the build; buffering and framebuffer are shared.
int arch_init();
void arch_putc(int c);
void arch_text_putc(int c);
TerminalSize arch_screen_size();
void arch_move_cursor(unsigned int row, unsigned int col);
void arch_clear_screen();
void arch_erase_line(unsigned int mode);
void arch_set_cursor_visible(bool visible);

}  // namespace cons
