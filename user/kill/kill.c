#include "../command.h"

static int parse_pid(char* text) {
    int pid = 0;
    if (!text[0])
        return -1;
    for (int i = 0; text[i]; ++i) {
        int digit = text[i] - '0';
        if (digit < 0 || digit > 9 || pid > (0x7FFFFFFF - digit) / 10)
            return -1;
        pid = pid * 10 + digit;
    }
    return pid > 0 ? pid : -1;
}

int main(int argc, char** argv) {
    if (argc < 2)
        return command_usage("Usage: kill PID... (positive decimal PIDs)\n");
    // Validate all operands before making any termination request.
    for (int i = 1; i < argc; ++i) {
        if (parse_pid(argv[i]) < 0)
            return command_usage("Usage: kill PID... (positive decimal PIDs)\n");
    }
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        long error = sys_kill(parse_pid(argv[i]));
        if (error < 0)
            status = command_error("kill", argv[i], error);
    }
    return status;
}
