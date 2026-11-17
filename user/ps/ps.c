#include "../command.h"

static const char* state_name(unsigned int state) {
    switch (state) {
        case PROCESS_STATE_UNINIT: return "U";
        case PROCESS_STATE_SLEEPING: return "S";
        case PROCESS_STATE_RUNNABLE: return "R";
        case PROCESS_STATE_RUNNING: return "R+";
        case PROCESS_STATE_ZOMBIE: return "Z";
        default: return "?";
    }
}

int main(int argc, char** argv) {
    (void)argv;
    if (argc != 1)
        return command_usage("Usage: ps\n");
    if (command_text(STDOUT_FD, "PID\tPPID\tSTATE\tTYPE\tNAME\n") < 0)
        return command_error("ps", 0, SYS_ERROR_IO);
    long cursor = -1;
    for (;;) {
        struct ProcessInfo info;
        long found = sys_process_next(cursor, &info);
        if (found < 0)
            return command_error("ps", 0, found);
        if (found == 0)
            return 0;
        if (printf("%d\t%d\t%s\t%s\t%s\n", info.pid, info.ppid, state_name(info.state),
                   info.kind == PROCESS_KIND_KERNEL ? "kernel" : "user", info.name) < 0)
            return command_error("ps", 0, SYS_ERROR_IO);
        cursor = info.pid;
    }
}
