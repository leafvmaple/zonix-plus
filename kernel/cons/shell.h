#pragma once

namespace shell {

using CommandCallback = void (*)(int argc, char** argv);

int register_command(const char* name, const char* desc, CommandCallback func);
void print_commands();

void init();
void handle_char(char c);
void prompt();
int main(void* arg);  // Shell process entry point (PID 2)

}  // namespace shell
