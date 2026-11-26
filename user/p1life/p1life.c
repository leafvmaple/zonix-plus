#include "minilib.h"

int check(int condition, char* message) {
    if (!condition) {
        char* prefix = "[FAIL] P1-1 user: ";
        sys_write(STDERR_FD, prefix, strlen(prefix));
        sys_write(STDERR_FD, message, strlen(message));
        sys_write(STDERR_FD, "\n", 1);
    }
    return condition;
}

int child(char* mode) {
    if (!strcmp(mode, "short"))
        return 7;
    if (!strcmp(mode, "fd")) {
        if (sys_write(3, "CD", 2) == 2 && sys_close(3) == 0)
            return 42;
        return 90;
    }
    if (!strcmp(mode, "spin")) {
        for (;;)
            sys_yield();
    }
    if (!strcmp(mode, "read")) {
        char c;
        return (int)sys_read(STDIN_FD, &c, 1);
    }
    if (!strcmp(mode, "raw")) {
        /* In raw mode Ctrl-C is a byte for the application, not an interrupt. */
        char c = 0;
        if (sys_ioctl(STDIN_FD, TERMINAL_SET_RAW, 1) != 0)
            return 80;
        if (sys_write(STDOUT_FD, "W", 1) != 1 || sys_read(STDIN_FD, &c, 1) != 1)
            return 81;
        if (sys_ioctl(STDIN_FD, TERMINAL_SET_RAW, 0) != 0)
            return 82;
        return c;
    }
    if (!strcmp(mode, "fault"))
        return *(int*)0x1000;
    return 91;
}

/* Runs as the shell of a pseudo-terminal session, with the terminal as its
 * standard streams. Each step that goes wrong has its own exit code. A single
 * letter written to the terminal tells the driver what to do next. */
int pty_shell(char* self) {
    char c;
    int status = 0;
    char* reader[3] = {self, "read", 0};
    long pid;

    /* The driver interrupts this job with Ctrl-C. */
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_BEGIN) != 0)
        return 60;
    if (sys_read(STDIN_FD, &c, 1) >= 0)
        return 61; /* The owner must not take the job's input. */
    pid = sys_spawn_foreground(self, 2, reader);
    if (pid <= 0 || sys_write(STDOUT_FD, "R", 1) != 1)
        return 62;
    if (sys_wait((int)pid, &status) != pid || status != PROCESS_STATUS_KILLED)
        return 63;
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_END) != SYS_ERROR_INTERRUPTED)
        return 64;

    /* The shell cancels this job itself, which is not reported as an interrupt. */
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_BEGIN) != 0)
        return 65;
    pid = sys_spawn_foreground(self, 2, reader);
    if (pid <= 0)
        return 66;
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_CANCEL) != 0)
        return 67;
    if (sys_spawn_foreground(self, 2, reader) != SYS_ERROR_INTERRUPTED)
        return 68; /* A cancelled job accepts no more tasks. */
    if (sys_wait((int)pid, &status) != pid || status != PROCESS_STATUS_KILLED)
        return 69;
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_END) != 0)
        return 70;

    /* The job's task reads the byte the driver types. */
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_BEGIN) != 0)
        return 71;
    pid = sys_spawn_foreground(self, 2, reader);
    if (pid <= 0 || sys_write(STDOUT_FD, "D", 1) != 1)
        return 72;
    if (sys_wait((int)pid, &status) != pid || status != 1)
        return 73;
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_END) != 0)
        return 74;

    /* A task in raw mode receives Ctrl-C as input and the job is not interrupted. */
    char* raw[3] = {self, "raw", 0};
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_BEGIN) != 0)
        return 77;
    pid = sys_spawn_foreground(self, 2, raw);
    if (pid <= 0)
        return 78;
    if (sys_wait((int)pid, &status) != pid || status != 3)
        return 79;
    if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_END) != 0)
        return 83;
    if (sys_write(STDOUT_FD, "E", 1) != 1)
        return 75;

    /* Closing the master ends the session and this task with it. */
    sys_read(STDIN_FD, &c, 1);
    return 76;
}

/* Returns the next byte the session wrote, or -1 once the session has ended. */
int pty_next(int master) {
    char c;
    long mask = 1;
    for (int i = 0; i < master; i = i + 1)
        mask = mask * 2;
    for (;;) {
        long count = sys_read(master, &c, 1);
        if (count == 1)
            return c;
        if (count == 0 || sys_wait_readable(mask) < 0)
            return -1;
    }
}

/* Opens a pseudo-terminal, checks its data path and drives a shell through
 * the three ways a foreground job ends. */
int pty_driver(char* self) {
    struct TerminalSize size;
    int ends[2];
    char data[8];
    int status = 0;
    size.rows = 24;
    size.cols = 80;
    if (!check(sys_openpty(ends, &size) == 0, "open pseudo-terminal"))
        return 1;
    int master = ends[0];
    int slave = ends[1];
    if (!check(sys_write(slave, "out", 3) == 3 && sys_read(master, data, 8) == 3 && !memcmp(data, "out", 3),
               "slave output reaches the master"))
        return 1;
    if (!check(sys_read(master, data, 1) < 0, "empty master read does not block"))
        return 1;
    size.rows = 0;
    size.cols = 0;
    if (!check(sys_ioctl(slave, TERMINAL_GET_SIZE, (long)&size) == 0 && size.rows == 24 && size.cols == 80,
               "slave reports the size it was opened with"))
        return 1;
    size.rows = 30;
    size.cols = 100;
    if (!check(sys_ioctl(master, TERMINAL_SET_SIZE, (long)&size) == 0, "master resizes the terminal"))
        return 1;
    size.rows = 0;
    if (!check(sys_ioctl(master, TERMINAL_SET_SIZE, (long)&size) < 0 &&
                   sys_ioctl(slave, TERMINAL_GET_SIZE, (long)&size) == 0 && size.rows == 30 && size.cols == 100,
               "invalid size is rejected and the last size kept"))
        return 1;
    if (!check(sys_read(slave, data, 1) < 0 && sys_ioctl(slave, TERMINAL_SET_RAW, 1) == SYS_ERROR_ACCESS,
               "a task outside the session cannot read its input or take raw mode"))
        return 1;
    if (!check(sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_BEGIN) == SYS_ERROR_ACCESS,
               "a background task cannot start a console job"))
        return 1;

    /* The shell gets the terminal as its standard streams. */
    int in = (int)sys_fcntl(STDIN_FD, F_DUPFD_CLOEXEC, 0);
    int out = (int)sys_fcntl(STDOUT_FD, F_DUPFD_CLOEXEC, 0);
    int err = (int)sys_fcntl(STDERR_FD, F_DUPFD_CLOEXEC, 0);
    char* shell_args[3] = {self, "pty-shell", 0};
    long shell = -1;
    if (in >= 0 && out >= 0 && err >= 0 && sys_dup2(slave, STDIN_FD) >= 0 && sys_dup2(slave, STDOUT_FD) >= 0 &&
        sys_dup2(slave, STDERR_FD) >= 0)
        shell = sys_spawn(self, 2, shell_args);
    int restored = sys_dup2(in, STDIN_FD) >= 0 && sys_dup2(out, STDOUT_FD) >= 0 && sys_dup2(err, STDERR_FD) >= 0;
    sys_close(in);
    sys_close(out);
    sys_close(err);
    if (!check(restored && shell > 0, "start a shell on the pseudo-terminal"))
        return 1;

    int passed = check(pty_next(master) == 'R', "shell starts a foreground job");
    data[0] = 3;
    passed = passed && check(sys_write(master, data, 1) == 1, "master accepts Ctrl-C");
    passed = passed && check(pty_next(master) == 'D', "Ctrl-C interrupts the job and a cancelled job ends quietly");
    data[0] = 'x';
    passed = passed && check(sys_write(master, data, 1) == 1, "master accepts input");
    passed = passed && check(pty_next(master) == 'W', "input reaches the job's task");
    data[0] = 3;
    passed = passed && check(sys_write(master, data, 1) == 1, "master accepts Ctrl-C in raw mode");
    passed = passed && check(pty_next(master) == 'E', "raw mode delivers Ctrl-C as input without interrupting the job");
    sys_close(master);
    long waited = sys_wait((int)shell, &status);
    if (!passed) {
        /* The shell's exit code names the step that failed. */
        if (waited == shell)
            return status;
        return 1;
    }
    if (!check(waited == shell && status == PROCESS_STATUS_KILLED, "closing the master ends the session"))
        return 1;
    sys_close(slave);
    return 0;
}

int main(int argc, char** argv) {
    char* self = "/mnt/P1LIFE.ELF";
    if (argc == 0)
        return 19;
    if (argc > 1) {
        if (!strcmp(argv[1], "pty"))
            return pty_driver(self);
        if (!strcmp(argv[1], "pty-shell"))
            return pty_shell(self);
        if (!strcmp(argv[1], "child")) {
            if (argc == 4 && !strcmp(argv[0], "custom argv0") && !strcmp(argv[2], "two words") &&
                !strcmp(argv[3], "") && argv[4] == 0)
                return 42;
            return 92;
        }
        return child(argv[1]);
    }
    int status = -1;
    char* args[5] = {"custom argv0", "child", "two words", "", 0};
    long pid = sys_spawn(self, 4, args);
    if (!check(pid > 0 && sys_wait((int)pid, &status) == pid && status == 42, "arguments and wait status"))
        return 1;
    if (!check(sys_getpid() > 0 && sys_wait((int)pid, &status) == SYS_ERROR_NOT_FOUND, "PID and repeated wait"))
        return 1;
    char* short_args[3] = {self, "short", 0};
    if (!check(sys_spawn("/NOFILE.ELF", 2, short_args) == SYS_ERROR_NOT_FOUND &&
                   sys_spawn((char*)((long)-1), 2, short_args) < 0 && sys_spawn(self, 2, (char**)((long)-1)) < 0 &&
                   sys_spawn(self, PROCESS_MAX_ARGS + 1, short_args) == SYS_ERROR_FULL,
               "invalid spawn requests"))
        return 1;
    char* bad_args[3] = {self, (char*)((long)-1), 0};
    if (!check(sys_spawn(self, 2, bad_args) < 0, "invalid argument string"))
        return 1;
    char long_arg[PROCESS_MAX_ARG_BYTES + 1];
    memset(long_arg, 'a', PROCESS_MAX_ARG_BYTES);
    long_arg[PROCESS_MAX_ARG_BYTES] = 0;
    char* long_args[3] = {self, long_arg, 0};
    if (!check(sys_spawn(self, 2, long_args) < 0, "argument byte limit"))
        return 1;
    pid = sys_spawn(self, 0, 0);
    if (!check(pid > 0 && sys_wait((int)pid, &status) == pid && status == 19, "empty argument vector"))
        return 1;
    pid = sys_spawn(self, 2, short_args);
    if (!check(pid > 0 && sys_wait((int)pid, (int*)((long)-1)) == SYS_ERROR_INVALID &&
                   sys_wait((int)pid, &status) == pid && status == 7,
               "bad wait pointer preserves child"))
        return 1;

    int fd = (int)sys_open("/P1FD.TMP", O_CREAT | O_TRUNC | O_RDWR);
    if (!check(fd == 3 && sys_write(fd, "ab", 2) == 2, "open shared descriptor"))
        return 1;
    char* fd_args[3] = {self, "fd", 0};
    pid = sys_spawn(self, 2, fd_args);
    char data[8];
    if (!check(pid > 0 && sys_wait((int)pid, &status) == pid && status == 42 && sys_read(fd, data, 1) == 0 &&
                   sys_write(fd, "E", 1) == 1 && sys_close(fd) == 0,
               "inherited offset and independent close"))
        return 1;
    fd = (int)sys_open("/P1FD.TMP", O_RDONLY);
    if (!check(fd == 3 && sys_read(fd, data, 5) == 5 && !memcmp(data, "abCDE", 5) && sys_close(fd) == 0,
               "shared description persists exact bytes"))
        return 1;
    fd = (int)sys_open("/P1FD.TMP", O_TRUNC | O_RDWR);
    pid = sys_spawn(self, 2, fd_args);
    if (!check(fd == 3 && pid > 0 && sys_close(fd) == 0 && sys_wait((int)pid, &status) == pid && status == 42,
               "child retains description after parent close"))
        return 1;

    struct FileStat st;
    struct DirectoryEntry entry;
    if (!check(sys_stat(self, &st) == 0 && st.type == FILE_TYPE_FILE && st.size > 0 && sys_stat("/mnt", &st) == 0 &&
                   st.type == FILE_TYPE_DIRECTORY && sys_stat(self, (struct FileStat*)((long)-1)) == SYS_ERROR_INVALID,
               "path metadata"))
        return 1;
    int found = 0;
    long reply;
    for (long i = 0;; i = i + 1) {
        reply = sys_readdir("/mnt", i, &entry);
        if (reply != 1)
            break;
        if (!strcmp(entry.name, "P1LIFE.ELF"))
            found = 1;
    }
    if (!check(found && reply == 0 && sys_readdir("/mnt", -1, &entry) == SYS_ERROR_INVALID, "directory enumeration"))
        return 1;
    int invalid = (int)sys_open("/P1BAD.TMP", O_CREAT | O_TRUNC | O_WRONLY);
    if (!check(invalid >= 0 && sys_write(invalid, "bad ELF", 7) == 7 && sys_close(invalid) == 0 &&
                   sys_spawn("/P1BAD.TMP", 2, short_args) == SYS_ERROR_INVALID,
               "invalid ELF cleanup"))
        return 1;
    char* fault_args[3] = {self, "fault", 0};
    pid = sys_spawn(self, 2, fault_args);
    if (!check(pid > 0 && sys_wait((int)pid, &status) == pid && status == -1, "user fault isolation"))
        return 1;
    char* spin_args[3] = {self, "spin", 0};
    pid = sys_spawn(self, 2, spin_args);
    if (!check(pid > 0 && sys_kill((int)pid) == 0 && sys_wait((int)pid, &status) == pid &&
                   status == PROCESS_STATUS_KILLED,
               "terminate runnable child"))
        return 1;
    for (int i = 0; i < 100; i = i + 1) {
        pid = sys_spawn(self, 2, short_args);
        if (!check(pid > 0 && sys_wait((int)pid, &status) == pid && status == 7, "100 spawn/wait cycles"))
            return 1;
    }
    /* Priorities: only the user range, only for itself or its children. */
    if (!check(sys_set_priority(0, PROCESS_PRIORITY_HIGHEST - 1) == SYS_ERROR_INVALID &&
                   sys_set_priority(0, PROCESS_PRIORITY_LOWEST + 1) == SYS_ERROR_INVALID &&
                   sys_set_priority(1, PROCESS_PRIORITY_DEFAULT) == SYS_ERROR_NOT_FOUND,
               "priority limits"))
        return 1;
    pid = sys_spawn(self, 2, short_args);
    if (!check(pid > 0 && sys_set_priority((int)pid, PROCESS_PRIORITY_LOWEST) == 0 &&
                   sys_wait((int)pid, &status) == pid && status == 7,
               "child priority"))
        return 1;
    if (!check(sys_set_priority(0, PROCESS_PRIORITY_HIGHEST) == 0 && sys_set_priority(0, PROCESS_PRIORITY_DEFAULT) == 0,
               "own priority"))
        return 1;
    char* done = "P1_1_USER_LIFECYCLE_OK\n";
    sys_write(STDOUT_FD, done, strlen(done));
    return 0;
}
