#pragma once

namespace cons {

int init();
int late_init();
char getc();
void putc(int c);
void push_input(char c);  // called by input ISRs (pl011, virtio_kbd, ...)

}  // namespace cons