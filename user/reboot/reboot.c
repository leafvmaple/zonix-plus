#include "../command.h"

int main(int argc, char** argv) {
    (void)argv;
    if (argc != 1)
        return command_usage("Usage: reboot\n");
    long error = sys_reboot();
    return error < 0 ? command_error("reboot", 0, error) : 0;
}
