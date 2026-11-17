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
    if (!strcmp(mode, "fault"))
        return *(int*)0x1000;
    return 91;
}

int main(int argc, char** argv) {
    char* self = "/mnt/P1LIFE.ELF";
    if (argc == 0)
        return 19;
    if (argc > 1) {
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
    char* done = "P1_1_USER_LIFECYCLE_OK\n";
    sys_write(STDOUT_FD, done, strlen(done));
    return 0;
}
