#pragma once

namespace pl011 {

int init();
void putc(int c);
int getc();
void intr();  // called from IRQ handler

}  // namespace pl011
