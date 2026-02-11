#pragma once

void shell_init(void);
void shell_handle_char(char c);
void shell_prompt(void);
int  shell_main(void *arg);  // Shell process entry point (PID 2)
