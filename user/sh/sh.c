#include "../command.h"

#define MAX_REDIRECTS 16
struct Redirect {
    int target;
    int flags;
    char* path;
};
struct Command {
    int argc;
    char* args[PROCESS_MAX_ARGS + 1];
    int redirect_count;
    struct Redirect redirects[MAX_REDIRECTS];
};

/* Words use a separate buffer so inserting NUL cannot erase an adjacent '<' or
 * '>'. Only unquoted operators are syntax; empty quoted arguments stay words. */
static char* word(const char** input, char** output, int* io_number) {
    const char* read = *input;
    char* start = *output;
    int quote = 0;
    int plain = 1;
    int digits = 1;
    while (*read && (quote || (*read != ' ' && *read != '\t' && *read != '<' && *read != '>'))) {
        int c = *read++;
        if (c == '\\' && quote != '\'') {
            if (!*read)
                return 0;
            c = *read++;
            plain = 0;
        } else if (c == quote) {
            quote = 0;
            plain = 0;
            continue;
        } else if (!quote && (c == '\'' || c == '"')) {
            quote = c;
            plain = 0;
            continue;
        }
        *(*output)++ = (char)c;
        if (c < '0' || c > '9')
            digits = 0;
    }
    if (quote)
        return 0;
    *io_number = plain && digits && *output != start && (*read == '<' || *read == '>');
    *(*output)++ = 0;
    *input = read;
    return start;
}

static int parse(const char* input, char* words, struct Command* command) {
    memset(command, 0, sizeof(*command));
    while (*input) {
        while (*input == ' ' || *input == '\t')
            ++input;
        if (!*input)
            break;
        int io_number;
        if (*input == '<' || *input == '>') {
            if (command->redirect_count == MAX_REDIRECTS)
                return -1;
            struct Redirect* redirect = &command->redirects[command->redirect_count++];
            int operation = *input++;
            redirect->target = operation == '<' ? STDIN_FD : STDOUT_FD;
            redirect->flags = operation == '<' ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
            if (operation == '>' && *input == '>') {
                redirect->flags = O_WRONLY | O_CREAT | O_APPEND;
                ++input;
            }
            while (*input == ' ' || *input == '\t')
                ++input;
            if (!*input || *input == '<' || *input == '>')
                return -1;
            redirect->path = word(&input, &words, &io_number);
            if (!redirect->path)
                return -1;
        } else {
            if (command->argc == PROCESS_MAX_ARGS)
                return -1;
            char* argument = word(&input, &words, &io_number);
            // This increment supports stdin/stdout operators, not 2> or FD aliases.
            if (!argument || io_number)
                return -1;
            command->args[command->argc++] = argument;
        }
    }
    return 0;
}

static int restore(int* saved) {
    int failed = 0;
    for (int target = STDIN_FD; target <= STDOUT_FD; ++target) {
        if (saved[target] >= 0) {
            if (sys_dup2(saved[target], target) < 0)
                failed = command_error("sh", "restore stdio", SYS_ERROR_IO);
            if (sys_close(saved[target]) < 0)
                failed = command_error("sh", "close saved stdio", SYS_ERROR_IO);
            saved[target] = -1;
        }
    }
    return failed;
}

static int redirect(struct Command* command, int* saved) {
    // Backups must not be inherited by a spawned program. Reserve them before
    // any open/truncate; every return is followed by restore() in main.
    for (int i = 0; i < command->redirect_count; ++i) {
        int target = command->redirects[i].target;
        if (saved[target] < 0) {
            long backup = sys_fcntl(target, F_DUPFD_CLOEXEC, 0);
            if (backup < 0)
                return command_error("sh", "save stdio", backup);
            saved[target] = (int)backup;
        }
    }
    for (int i = 0; i < command->redirect_count; ++i) {
        struct Redirect* action = &command->redirects[i];
        int fd = sys_open(action->path, action->flags | O_CLOEXEC);
        if (fd < 0)
            return command_error("sh", action->path, fd);
        long error = sys_dup2(fd, action->target);
        int closed = sys_close(fd);
        if (error < 0 || closed < 0)
            return command_error("sh", action->path, error < 0 ? error : SYS_ERROR_IO);
    }
    return 0;
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

static int run(struct Command* command, int previous, int* quit, int* interrupted) {
    int argc = command->argc;
    char** args = command->args;
    if (argc == 0)
        return 0;
    if (!strcmp(args[0], "exit")) {
        int code = argc == 2 ? exit_code(args[1]) : previous;
        if (argc > 2 || (argc == 2 && code < 0))
            return command_usage("sh: usage: exit [0..255]\n");
        *quit = 1;
        return code;
    }
    if (!strcmp(args[0], "status")) {
        if (printf("%d\n", previous) < 0)
            return command_error("sh", 0, SYS_ERROR_IO);
        return previous;
    }
    if (!strcmp(args[0], "help")) {
        if (printf("help pwd cd [directory] echo [args] status exit [code]\n") < 0 ||
            printf("Base commands: ls cat cp mkdir rm sync shutdown ps kill vi\n") < 0 ||
            printf("Programs: name (search /bin, /opt/bin), ./name or /absolute/path\n") < 0 ||
            printf("Quotes and backslash escape arguments. Foreground execution only.\n") < 0 ||
            printf("Redirection: < input, > output, >> append (stdin/stdout only).\n") < 0)
            return command_error("sh", 0, SYS_ERROR_IO);
        return 0;
    }
    if (!strcmp(args[0], "pwd")) {
        char cwd[PROCESS_MAX_PATH];
        long error = sys_getcwd(cwd, sizeof(cwd));
        if (error < 0)
            return command_error("sh", "pwd", error);
        return printf("%s\n", cwd) < 0 ? command_error("sh", 0, SYS_ERROR_IO) : 0;
    }
    if (!strcmp(args[0], "cd")) {
        if (argc > 2)
            return command_usage("sh: usage: cd [directory]\n");
        char* path = argc == 2 ? args[1] : "/";
        long error = sys_chdir(path);
        return error < 0 ? command_error("sh", path, error) : 0;
    }
    if (!strcmp(args[0], "echo")) {
        for (int i = 1; i < argc; ++i) {
            if ((i > 1 && command_text(STDOUT_FD, " ") < 0) || command_text(STDOUT_FD, args[i]) < 0)
                return command_error("sh", 0, SYS_ERROR_IO);
        }
        return command_text(STDOUT_FD, "\n") < 0 ? command_error("sh", 0, SYS_ERROR_IO) : 0;
    }
    char path[PROCESS_MAX_PATH];
    long pid = program_path(args[0], path);
    if (pid == 0)
        pid = sys_spawn_foreground(path, argc, args);
    if (pid == SYS_ERROR_INTERRUPTED) {
        *interrupted = 1;
        return PROCESS_STATUS_KILLED;
    }
    if (pid < 0) {
        command_error("sh", args[0], pid);
        return 127;
    }
    int status;
    long waited = sys_wait((int)pid, &status);
    if (waited < 0)
        return command_error("sh", "wait", waited);
    *interrupted = status == PROCESS_STATUS_KILLED;
    return status;
}

int main() {
    char line[257];
    char words[257];
    char cwd[PROCESS_MAX_PATH];
    struct Command command;
    int last = 0;
    int skip_lf = 0;
    printf("Zonix shell (PID %d). Type help.\n", (int)sys_getpid());
    for (;;) {
        if (sys_getcwd(cwd, sizeof(cwd)) < 0)
            memcpy(cwd, "?", 2);
        if (printf("%s $ ", cwd) < 0)
            return command_error("sh", "prompt", SYS_ERROR_IO);
        long size = command_read_line(line, sizeof(line), &skip_lf);
        if (size == 0)
            return last;
        if (size == SYS_ERROR_INTERRUPTED) {
            last = PROCESS_STATUS_KILLED;
            continue;
        }
        if (size == SYS_ERROR_FULL) {
            command_text(STDERR_FD, "sh: line too long\n");
            last = 2;
            continue;
        }
        if (size < 0)
            return command_error("sh", "stdin", size);
        if (line[size - 1] == '\n')
            line[size - 1] = 0;
        if (parse(line, words, &command) < 0) {
            command_text(STDERR_FD, "sh: invalid quoting, redirection or too many arguments\n");
            last = 2;
            continue;
        }
        if (command.argc == 0 && command.redirect_count == 0)
            continue;
        int saved[2] = {-1, -1};
        int quit = 0;
        int interrupted = 0;
        last = redirect(&command, saved) ? 1 : run(&command, last, &quit, &interrupted);
        if (restore(saved))
            return 1;  // Init starts a fresh shell rather than using damaged stdio.
        if (interrupted)
            command_text(STDOUT_FD, "^C\n");
        if (quit)
            return last;
    }
}
