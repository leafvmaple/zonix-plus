#include "../command.h"

int main(int argc, char** argv) {
    (void)argv;
    if (argc != 1)
        return command_usage("Usage: sync\n");
    long error = sys_sync();
    return error < 0 ? command_error("sync", 0, error) : 0;
}
