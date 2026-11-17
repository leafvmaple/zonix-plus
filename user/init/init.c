#include "minilib.h"

int main() {
    printf("Zonix userspace init (PID %d)\n", (int)sys_getpid());
    sys_chdir("/user");
    char* args[2] = {"/user/SH.ELF", 0};
    long shell = 0;
    int status;
    char retry;
    for (;;) {
        if (shell <= 0) {
            shell = sys_spawn(args[0], 1, args);
            if (shell < 0) {
                printf("init: cannot start shell (%d); press Enter to retry\n", (int)shell);
                sys_read(STDIN_FD, &retry, 1);
                continue;
            }
        }
        long child = sys_wait(0, &status);
        if (child == shell) {
            printf("init: shell exited (%d), restarting\n", status);
            shell = 0;
        } else if (child > 0) {
            printf("init: reaped orphan %d (%d)\n", (int)child, status);
        } else {
            printf("init: wait failed (%d); press Enter to retry\n", (int)child);
            sys_read(STDIN_FD, &retry, 1);
        }
    }
}
