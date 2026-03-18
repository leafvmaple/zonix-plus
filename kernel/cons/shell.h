#pragma once

namespace shell {

using command_func_t = void (*)(int argc, char** argv);

int register_command(const char* name, const char* desc, command_func_t func);

void init();
void handle_char(char c);
void prompt();
int main(void* arg);  // Shell process entry point (PID 2)

}  // namespace shell
