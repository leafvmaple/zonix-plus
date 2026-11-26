#include "../command.h"
#include "line_editor.h"

#define MAX_REDIRECTS 16
#define MAX_STAGES 8
struct Redirect {
    int target;
    int flags;
    char* path;
};
struct Command {
    const char* source;
    int source_length;
    int argc;
    char* args[PROCESS_MAX_ARGS + 1];
    int redirect_count;
    struct Redirect redirects[MAX_REDIRECTS];
};

/* Words use a separate buffer so inserting NUL cannot erase an adjacent '<',
 * '>' or '|'. Only unquoted operators are syntax; empty quoted arguments stay words. */
static char* word(const char** input, char** output, int* io_number) {
    const char* read = *input;
    char* start = *output;
    int quote = 0;
    int plain = 1;
    int digits = 1;
    while (*read && (quote || (*read != ' ' && *read != '\t' && *read != '<' && *read != '>' && *read != '|'))) {
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

static int parse(const char* input, char* words, struct Command* commands) {
    memset(commands, 0, MAX_STAGES * sizeof(*commands));
    int stages = 1;
    struct Command* command = commands;
    command->source = input;
    while (*input) {
        while (*input == ' ' || *input == '\t')
            ++input;
        if (!*input)
            break;
        int io_number;
        if (*input == '|') {
            if (command->argc == 0 || stages == MAX_STAGES)
                return -1;
            command->source_length = (int)(input - command->source);
            ++input;
            command = &commands[stages++];
            command->source = input;
        } else if (*input == '<' || *input == '>') {
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
            if (!*input || *input == '<' || *input == '>' || *input == '|')
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
    command->source_length = (int)(input - command->source);
    return stages > 1 && command->argc == 0 ? -1 : stages;
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

static int is_builtin(char* name) {
    return !strcmp(name, "exit") || !strcmp(name, "status") || !strcmp(name, "help") || !strcmp(name, "pwd") ||
           !strcmp(name, "cd") || !strcmp(name, "echo") || !strcmp(name, "shutdown") || !strcmp(name, "reboot");
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
        if (printf("help pwd cd [directory] echo [args] status exit [code] shutdown reboot\n") < 0 ||
            printf("Base commands: ls cat cp mkdir rm sync ps kill vi\n") < 0 ||
            printf("Programs: name (search /bin, /opt/bin), ./name or /absolute/path\n") < 0 ||
            printf("Quotes and backslash escape arguments. Foreground execution only.\n") < 0 ||
            printf("Edit: Left/Right, Home/End, Delete/Backspace; Up/Down history (16 lines).\n") < 0 ||
            printf("Redirection: < input, > output, >> append (stdin/stdout only).\n") < 0 ||
            printf("Pipelines: command | command (up to 8 stages; status is the last stage).\n") < 0)
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
    /* Built in so they need no program from disk: they still work after the
     * root disk has failed and nothing else can be loaded. */
    if (!strcmp(args[0], "shutdown") || !strcmp(args[0], "reboot")) {
        int reboot = args[0][0] == 'r';
        if (argc != 1)
            return command_usage(reboot ? "Usage: reboot\n" : "Usage: shutdown\n");
        long error = reboot ? sys_reboot() : sys_shutdown();
        return error < 0 ? command_error(args[0], 0, error) : 0;
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

static int reset_stdio(int* saved) {
    int failed = 0;
    for (int i = 0; i < 2; ++i)
        if (sys_dup2(saved[i], i) < 0)
            failed = command_error("sh", "restore stdio", SYS_ERROR_IO);
    return failed;
}

static int close_pipe(int* fd) {
    int failed = 0;
    if (*fd >= 0) {
        if (sys_close(*fd) < 0)
            failed = command_error("sh", "close pipe", SYS_ERROR_IO);
        *fd = -1;
    }
    return failed;
}

static int pipeline(struct Command* commands, int count, int* interrupted, int* damaged) {
    int pids[MAX_STAGES];
    int started = 0;
    int saved[2] = {-1, -1};
    int previous = -1;
    int next[2] = {-1, -1};
    int result = 0;
    // Reserve stdio backups before opening/truncating any redirected target.
    for (int i = 0; i < 2; ++i) {
        long fd = sys_fcntl(i, F_DUPFD_CLOEXEC, 0);
        if (fd < 0) {
            result = command_error("sh", "save stdio", fd);
            if (restore(saved))
                *damaged = 1;
            return result;
        }
        saved[i] = (int)fd;
    }
    long error = sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_BEGIN);
    if (error < 0) {
        result = error == SYS_ERROR_INTERRUPTED ? PROCESS_STATUS_KILLED : command_error("sh", "foreground job", error);
        *interrupted = error == SYS_ERROR_INTERRUPTED;
        if (restore(saved))
            *damaged = 1;
        return result;
    }
    for (int i = 0; i < count; ++i) {
        struct Command* command = &commands[i];
        if (i + 1 < count && (error = sys_pipe2(next, O_CLOEXEC)) < 0) {
            result = command_error("sh", "pipe", error);
            break;
        }
        if ((previous >= 0 && sys_dup2(previous, STDIN_FD) < 0) ||
            (next[1] >= 0 && sys_dup2(next[1], STDOUT_FD) < 0)) {
            result = command_error("sh", "pipe stdio", SYS_ERROR_IO);
            break;
        }
        char path[PROCESS_MAX_PATH];
        char stage[257];
        char* child_args[4] = {"sh", "-c", stage, 0};
        if (is_builtin(command->args[0])) {
            memcpy(stage, (void*)command->source, command->source_length);
            stage[command->source_length] = 0;
            error = sys_spawn_foreground("/bin/sh", 3, child_args);
        } else if (redirect(command, saved)) {
            result = 1;
            break;
        } else {
            error = program_path(command->args[0], path);
            if (error == 0)
                error = sys_spawn_foreground(path, command->argc, command->args);
        }
        if (error < 0) {
            if (error == SYS_ERROR_INTERRUPTED) {
                *interrupted = 1;
                result = PROCESS_STATUS_KILLED;
            } else {
                command_error("sh", command->args[0], error);
                result = 127;
            }
            break;
        }
        pids[started++] = (int)error;
        // Drop *all* parent copies of this stage's write/read endpoints now.
        // Hidden descriptors are CLOEXEC; only the selected 0/1 reach the child.
        if (reset_stdio(saved)) {
            *damaged = 1;
            result = 1;
        }
        if (close_pipe(&previous))
            result = 1;
        if (close_pipe(&next[1]))
            result = 1;
        previous = next[0];
        next[0] = -1;
        if (result)
            break;
    }
    if (restore(saved)) {
        *damaged = 1;
        result = 1;
    }
    if (close_pipe(&previous))
        result = 1;
    if (close_pipe(&next[0]))
        result = 1;
    if (close_pipe(&next[1]))
        result = 1;
    if (result && sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_CANCEL) < 0)
        *damaged = 1;
    int last_status = 0;
    for (int i = 0; i < started; ++i) {
        int status = 0;
        error = sys_wait(pids[i], &status);
        if (error < 0) {
            result = command_error("sh", "wait", error);
            if (sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_CANCEL) < 0)
                *damaged = 1;
        } else if (i + 1 == count)
            last_status = status;
    }
    error = sys_ioctl(STDERR_FD, TERMINAL_JOB, TERMINAL_JOB_END);
    if (error == SYS_ERROR_INTERRUPTED) {
        *interrupted = 1;
        return PROCESS_STATUS_KILLED;
    }
    if (error < 0) {
        *damaged = 1;
        return command_error("sh", "finish foreground job", error);
    }
    return result ? result : last_status;
}

int main(int argc, char** argv) {
    char line[257];
    char words[257];
    char cwd[PROCESS_MAX_PATH];
    struct Command commands[MAX_STAGES];
    struct LineEditor editor = {0};
    int last = 0;
    int skip_lf = 0;
    int once = argc == 3 && !strcmp(argv[1], "-c");
    if (argc > 1 && !once)
        return command_usage("Usage: sh [-c command]\n");
    if (!once)
        printf("Zonix shell (PID %d). Type help.\n", (int)sys_getpid());
    for (;;) {
        long size;
        if (once) {
            size = strlen(argv[2]);
            if (size > 255)
                return command_usage("sh: command too long\n");
            memcpy(line, argv[2], size + 1);
            if (!size)
                return 0;
        } else {
            if (sys_getcwd(cwd, sizeof(cwd)) < 0)
                memcpy(cwd, "?", 2);
            size = line_editor_read(&editor, line, cwd, &skip_lf);
        }
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
        int stages = parse(line, words, commands);
        if (stages < 0) {
            command_text(STDERR_FD, "sh: invalid pipeline, quoting, redirection or argument limit\n");
            last = 2;
            if (once)
                return last;
            continue;
        }
        if (commands[0].argc == 0 && commands[0].redirect_count == 0) {
            if (once)
                return 0;
            continue;
        }
        int saved[2] = {-1, -1};
        int quit = 0;
        int interrupted = 0;
        int damaged = 0;
        if (stages > 1)
            last = pipeline(commands, stages, &interrupted, &damaged);
        else
            last = redirect(commands, saved) ? 1 : run(commands, last, &quit, &interrupted);
        if (restore(saved) || damaged)
            return 1;  // Init starts a fresh shell rather than using damaged stdio.
        if (interrupted)
            command_text(STDOUT_FD, "^C\n");
        if (quit || once)
            return last;
    }
}
