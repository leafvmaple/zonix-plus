#include "../command.h"

int main(int argc, char** argv) {
    if (argc < 2)
        return command_usage("Usage: mkdir directory...\n");
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        long error = sys_mkdir(argv[i]);
        if (error < 0)
            status = command_error("mkdir", argv[i], error);
    }
    return status;
}
