#include "minilib.h"

// An application disk may list background services, one command per line.
#define SERVICES_PATH      "/opt/etc/services"
#define SERVICES_MAX_BYTES 1024
#define MAX_SERVICES       8
#define MAX_SERVICE_ARGS   8

static int is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

// Splits a line in place into at most MAX_SERVICE_ARGS words.
static int split_words(char* line, char** argv) {
    int argc = 0;
    char* cursor = line;
    while (argc < MAX_SERVICE_ARGS) {
        while (is_blank(*cursor))
            *cursor++ = 0;
        if (*cursor == 0)
            break;
        argv[argc++] = cursor;
        while (*cursor && !is_blank(*cursor))
            ++cursor;
    }
    argv[argc] = 0;
    return *cursor == 0 ? argc : -1;
}

static int start_services(long* pids) {
    int fd = sys_open(SERVICES_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    char text[SERVICES_MAX_BYTES + 1];
    long size = 0;
    long count;
    while (size < SERVICES_MAX_BYTES && (count = sys_read(fd, text + size, SERVICES_MAX_BYTES - size)) > 0)
        size += count;
    sys_close(fd);
    text[size] = 0;

    int started = 0;
    char* line = text;
    while (*line) {
        char* end = line;
        while (*end && *end != '\n')
            ++end;
        char* next = *end ? end + 1 : end;
        *end = 0;
        char* first = line;
        while (is_blank(*first))
            ++first;
        char* argv[MAX_SERVICE_ARGS + 1];
        int argc = *first == '#' ? 0 : split_words(line, argv);
        if (argc != 0) {
            if (argc < 0 || argv[0][0] != '/') {
                printf("init: ignoring service line in %s\n", SERVICES_PATH);
            } else if (started == MAX_SERVICES) {
                printf("init: more than %d services, ignoring %s\n", MAX_SERVICES, argv[0]);
            } else {
                long pid = sys_spawn(argv[0], argc, argv);
                if (pid < 0) {
                    printf("init: cannot start %s (%d)\n", argv[0], (int)pid);
                } else {
                    printf("init: started %s (PID %d)\n", argv[0], (int)pid);
                    pids[started++] = pid;
                }
            }
        }
        line = next;
    }
    return started;
}

int main() {
    printf("Zonix userspace init (PID %d)\n", (int)sys_getpid());
    long services[MAX_SERVICES];
    int service_count = start_services(services);
    char* args[2] = {"/bin/sh", 0};
    long shell = 0;
    int status;
    char retry;
    for (;;) {
        if (shell <= 0) {
            shell = sys_spawn_foreground(args[0], 1, args);
            if (shell < 0) {
                printf("init: cannot start shell (%d); press Enter to retry\n", (int)shell);
                sys_read(STDIN_FD, &retry, 1);
                continue;
            }
        }
        long child = sys_wait(0, &status);
        int service = 0;
        while (service < service_count && services[service] != child)
            ++service;
        if (child == shell) {
            printf("init: shell exited (%d), restarting\n", status);
            shell = 0;
        } else if (child > 0 && service < service_count) {
            // Services are not restarted; the owner can start one again from the shell.
            printf("init: service %d exited (%d)\n", (int)child, status);
            services[service] = services[--service_count];
        } else if (child > 0) {
            printf("init: reaped orphan %d (%d)\n", (int)child, status);
        } else {
            printf("init: wait failed (%d); press Enter to retry\n", (int)child);
            sys_read(STDIN_FD, &retry, 1);
        }
    }
}
