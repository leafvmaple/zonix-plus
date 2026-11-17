#include "minilib.h"

/* Compact words in place; quotes preserve spaces and empty arguments. */
int split(char* line, char** args) {
    int read = 0;
    int write = 0;
    int count = 0;
    while (line[read]) {
        while (line[read] == ' ' || line[read] == '\t')
            read = read + 1;
        if (!line[read])
            break;
        if (count == PROCESS_MAX_ARGS)
            return -1;
        args[count] = line + write;
        count = count + 1;
        int quote = 0;
        while (line[read]) {
            int c = line[read];
            if (!quote && (c == ' ' || c == '\t'))
                break;
            read = read + 1;
            if (c == '\\' && quote != '\'') {
                if (!line[read])
                    return -1;
                c = line[read];
                read = read + 1;
            } else if (c == quote) {
                quote = 0;
                continue;
            } else if (!quote && (c == '\'' || c == '"')) {
                quote = c;
                continue;
            }
            line[write] = (char)c;
            write = write + 1;
        }
        if (quote)
            return -1;
        while (line[read] == ' ' || line[read] == '\t')
            read = read + 1;
        line[write] = 0;
        write = write + 1;
    }
    args[count] = 0;
    return count;
}

/* Console supplies raw bytes. EOF on an empty line exits; overflow is drained. */
int read_line(char* line, int capacity, int* skip_lf) {
    int size = 0;
    int overflow = 0;
    char c;
    for (;;) {
        if (sys_read(STDIN_FD, &c, 1) <= 0)
            return -1;
        if (*skip_lf && c == '\n') {
            *skip_lf = 0;
            continue;
        }
        *skip_lf = 0;
        if (c == '\r' || c == '\n') {
            if (c == '\r')
                *skip_lf = 1;
            sys_write(STDOUT_FD, "\n", 1);
            line[size] = 0;
            if (overflow)
                return -2;
            return size;
        }
        if (c == 3) {
            line[0] = 0;
            sys_write(STDOUT_FD, "^C\n", 3);
            return 0;
        }
        if (c == 4 && size == 0 && !overflow)
            return -1;
        if ((c == 8 || c == 127) && !overflow) {
            if (size > 0) {
                size = size - 1;
                sys_write(STDOUT_FD, "\b \b", 3);
            }
        } else if (c >= 32 || c == '\t') {
            if (size + 1 >= capacity)
                overflow = 1;
            else if (!overflow) {
                line[size] = c;
                size = size + 1;
                sys_write(STDOUT_FD, &c, 1);
            }
        }
    }
}

int exit_code(char* text) {
    int value = 0;
    if (!text[0])
        return -1;
    for (int i = 0; text[i]; i = i + 1) {
        if (text[i] < '0' || text[i] > '9')
            return -1;
        value = value * 10 + text[i] - '0';
        if (value > 255)
            return -1;
    }
    return value;
}

int program_path(char* name, char* path) {
    for (int i = 0; name[i]; i = i + 1) {
        if (name[i] == '/') {
            if (strlen(name) >= PROCESS_MAX_PATH)
                return SYS_ERROR_FULL;
            memcpy(path, name, strlen(name) + 1);
            return 0;
        }
    }
    char* directories[2] = {"/bin/", "/opt/bin/"};
    struct FileStat st;
    for (int i = 0; i < 2; i = i + 1) {
        long prefix = strlen(directories[i]);
        if (prefix + strlen(name) >= PROCESS_MAX_PATH)
            return SYS_ERROR_FULL;
        memcpy(path, directories[i], prefix);
        memcpy(path + prefix, name, strlen(name) + 1);
        long found = sys_stat(path, &st);
        if (found == 0)
            return 0;
        if (found != SYS_ERROR_NOT_FOUND)
            return (int)found;
    }
    return SYS_ERROR_NOT_FOUND;
}

int main() {
    char line[256];
    char cwd[PROCESS_MAX_PATH];
    char path[PROCESS_MAX_PATH];
    char* args[PROCESS_MAX_ARGS + 1];
    int last = 0;
    int skip_lf = 0;
    printf("Zonix shell (PID %d). Type help.\n", (int)sys_getpid());
    for (;;) {
        if (sys_getcwd(cwd, PROCESS_MAX_PATH) < 0)
            memcpy(cwd, "?", 2);
        printf("%s $ ", cwd);
        int size = read_line(line, 256, &skip_lf);
        if (size == -1)
            return last;
        if (size == -2) {
            printf("sh: line too long\n");
            last = 2;
            continue;
        }
        int argc = split(line, args);
        if (argc < 0) {
            printf("sh: invalid quoting or too many arguments\n");
            last = 2;
            continue;
        }
        if (argc == 0)
            continue;
        if (!strcmp(args[0], "exit")) {
            int code = last;
            if (argc == 2)
                code = exit_code(args[1]);
            if (argc > 2 || (argc == 2 && code < 0)) {
                printf("sh: usage: exit [0..255]\n");
                last = 2;
                continue;
            }
            return code;
        }
        if (!strcmp(args[0], "status")) {
            printf("%d\n", last);
            continue;
        }
        if (!strcmp(args[0], "help")) {
            printf("help pwd cd [directory] echo [args] status exit [code]\n");
            printf("Programs: name (search /bin, /opt/bin), ./name or /absolute/path\n");
            printf("Quotes and backslash escape arguments. Foreground execution only.\n");
            last = 0;
        } else if (!strcmp(args[0], "pwd")) {
            printf("%s\n", cwd);
            last = 0;
        } else if (!strcmp(args[0], "cd")) {
            char* target = "/";
            if (argc == 2)
                target = args[1];
            if (argc > 2) {
                printf("sh: usage: cd [directory]\n");
                last = 2;
            } else {
                long changed = sys_chdir(target);
                last = 0;
                if (changed < 0) {
                    printf("sh: cd failed (%d)\n", (int)changed);
                    last = 1;
                }
            }
        } else if (!strcmp(args[0], "echo")) {
            for (int i = 1; i < argc; i = i + 1) {
                if (i > 1)
                    sys_write(STDOUT_FD, " ", 1);
                sys_write(STDOUT_FD, args[i], strlen(args[i]));
            }
            sys_write(STDOUT_FD, "\n", 1);
            last = 0;
        } else {
            long pid = program_path(args[0], path);
            if (pid == 0)
                pid = sys_spawn(path, argc, args);
            if (pid < 0) {
                printf("sh: cannot run %s (%d)\n", args[0], (int)pid);
                last = 127;
            } else if (sys_wait((int)pid, &last) < 0) {
                printf("sh: wait failed\n");
                last = 1;
            }
        }
    }
}
