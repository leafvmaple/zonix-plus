#include "../command.h"

// An application disk may list background services, one command per line.
#define SERVICES_PATH      "/opt/etc/services"
#define SERVICES_MAX_BYTES 1024
#define MAX_SERVICES       8
#define MAX_SERVICE_ARGS   8
// The base system's log writer starts before anything else.
#define LOGD_PATH "/bin/logd"
// Services print into this file, not over the shell on the console. Past
// SERVICE_LOG_LIMIT at boot it becomes services.old.
#define SERVICE_LOG_PATH  "/var/log/services.log"
#define SERVICE_OLD_PATH  "/var/log/services.old"
#define SERVICE_OLD_NAME  "services.old"
#define SERVICE_LOG_LIMIT (1024L * 1024L)

static void log_event(int priority, const char* what, const char* name, long number) {
    char text[160];
    long size = 0;
    for (const char* part = what; *part && size < 100; ++part)
        text[size++] = *part;
    for (const char* part = name; *part && size < 150; ++part)
        text[size++] = *part;
    text[size++] = ' ';
    char digits[12];
    int count = 0;
    unsigned long value = number < 0 ? (unsigned long)-number : (unsigned long)number;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < 11);
    if (number < 0)
        text[size++] = '-';
    while (count > 0)
        text[size++] = digits[--count];
    text[size] = 0;
    command_log(priority, text);
}

static long start_logd(void) {
    struct FileStat stat;
    if (sys_stat(LOGD_PATH, &stat) < 0)
        return 0;
    char* argv[] = {LOGD_PATH, 0};
    long pid = sys_spawn(argv[0], 1, argv);
    if (pid < 0)
        printf("init: cannot start %s (%d)\n", argv[0], (int)pid);
    return pid > 0 ? pid : 0;
}

// Returns the service log opened for appending, or -1 to leave services on the console.
static int open_service_log(void) {
    sys_mkdir("/var");
    sys_mkdir("/var/log");
    struct FileStat stat;
    if (sys_stat(SERVICE_LOG_PATH, &stat) == 0 && (long)stat.size >= SERVICE_LOG_LIMIT) {
        sys_unlink(SERVICE_OLD_PATH);
        sys_rename(SERVICE_LOG_PATH, SERVICE_OLD_NAME);
    }
    int log = sys_open(SERVICE_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC);
    if (log < 0) {
        printf("init: cannot open %s (%d); services print on the console\n", SERVICE_LOG_PATH, log);
        return -1;
    }
    command_text(log, "---- boot ----\n");
    return log;
}

// Starts a service with `log` as its standard output and error; init keeps the console.
static long spawn_service(char** argv, int argc, int log) {
    if (log < 0)
        return sys_spawn(argv[0], argc, argv);
    int saved_output = (int)sys_fcntl(STDOUT_FD, F_DUPFD_CLOEXEC, 3);
    int saved_error = (int)sys_fcntl(STDERR_FD, F_DUPFD_CLOEXEC, 3);
    long pid = SYS_ERROR_IO;
    if (saved_output >= 0 && saved_error >= 0 && sys_dup2(log, STDOUT_FD) >= 0 && sys_dup2(log, STDERR_FD) >= 0)
        pid = sys_spawn(argv[0], argc, argv);
    if (saved_output >= 0) {
        sys_dup2(saved_output, STDOUT_FD);
        sys_close(saved_output);
    }
    if (saved_error >= 0) {
        sys_dup2(saved_error, STDERR_FD);
        sys_close(saved_error);
    }
    return pid;
}

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

static int start_services(long* pids, int capacity) {
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
    int log = -2;  // Opened with the first service, so a base system has no service log.
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
            } else if (started == capacity) {
                printf("init: more than %d services, ignoring %s\n", MAX_SERVICES, argv[0]);
            } else {
                if (log == -2)
                    log = open_service_log();
                long pid = spawn_service(argv, argc, log);
                if (pid < 0) {
                    printf("init: cannot start %s (%d)\n", argv[0], (int)pid);
                } else {
                    printf("init: started %s (PID %d)\n", argv[0], (int)pid);
                    log_event(6, "init: started service ", argv[0], pid);
                    pids[started++] = pid;
                }
            }
        }
        line = next;
    }
    if (log >= 0)
        sys_close(log);
    return started;
}

int main() {
    printf("Zonix userspace init (PID %d)\n", (int)sys_getpid());
    long services[MAX_SERVICES];
    // logd is watched like a service, ahead of those the application disk lists.
    long logd = start_logd();
    int service_count = 0;
    if (logd > 0)
        services[service_count++] = logd;
    service_count += start_services(services + service_count, MAX_SERVICES - service_count);
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
            printf("init: service %d exited (%d), see %s\n", (int)child, status, SERVICE_LOG_PATH);
            log_event(4, "init: service exited, status", "", status);
            services[service] = services[--service_count];
        } else if (child > 0) {
            printf("init: reaped orphan %d (%d)\n", (int)child, status);
        } else {
            printf("init: wait failed (%d); press Enter to retry\n", (int)child);
            sys_read(STDIN_FD, &retry, 1);
        }
    }
}
