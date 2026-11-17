#include "../command.h"

int main(int argc, char** argv) {
    (void)argv;
    if (argc != 1)
        return command_usage("Usage: shutdown\n");
    long error = sys_shutdown();
    return error < 0 ? command_error("shutdown", 0, error) : 0;
}
