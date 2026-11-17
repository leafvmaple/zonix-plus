#pragma once
#include <abi/terminal.h>

namespace terminal {
void putc(int c);
TerminalSize size();
void restore();
}  // namespace terminal
