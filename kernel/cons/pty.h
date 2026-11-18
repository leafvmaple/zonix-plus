#pragma once

#include "lib/result.h"
#include <sys/memory.hpp>
#include <abi/terminal.h>

struct Task;

namespace pty {
class Session;
struct Release {
    void operator()(Session* session) const noexcept;
};
using SessionRef = sys::unique_ptr<Session, Release>;

Error open(Task* task, int (&ends)[2], TerminalSize size);
// Checked at publication, under the same interrupt guard as physical foreground handoff.
Result<bool> prepare_spawn(Task* parent, Task* child, bool foreground);
void task_exited(Task* task);
}  // namespace pty
