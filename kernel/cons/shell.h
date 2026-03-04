#pragma once

namespace shell {

void init();
void handle_char(char c);
void prompt();
int main(void* arg);  // Shell process entry point (PID 2)

}  // namespace shell
