#pragma once

#include "lib/result.h"
#include <abi/terminal.h>

struct DisplayEvent;
struct Task;
namespace vfs {
class File;
}

namespace cons {

int init();
int late_init();
char getc();
Result<char> read_input();
void putc(int c);
void write_output(int c);
void key_event(uint16_t code, int value);
void pointer_event(unsigned int type, unsigned int code, int value);
Error set_raw(bool enabled);
Error acquire_display(vfs::File* owner, bool events);
Error read_display_event(const vfs::File* owner, DisplayEvent& event);
void release_display(vfs::File* owner);
bool owns_display(const vfs::File* owner);
void emergency_restore();
TerminalSize screen_size();
void push_input(char c);  // serial input
// Called with interrupts disabled at task publication / exit.
Error prepare_spawn(Task* parent, Task* child, bool foreground);
Error control_job(unsigned int action);
void task_exited(Task* task);

// Hardware backends selected by the build; buffering and framebuffer are shared.
int arch_init();
void arch_putc(int c);
void arch_text_putc(int c);
TerminalSize arch_screen_size();
void arch_move_cursor(unsigned int row, unsigned int col);
void arch_move_column(unsigned int col);
void arch_clear_screen();
void arch_erase_line(unsigned int mode);
void arch_set_cursor_visible(bool visible);

}  // namespace cons
