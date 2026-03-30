#pragma once

namespace shell {

using fnCommand = void (*)(int argc, char** argv);

int register_command(const char* name, const char* desc, fnCommand func);

void init();
void handle_char(char c);
void prompt();
int main(void* arg);  // Shell process entry point (PID 2)

}  // namespace shell
