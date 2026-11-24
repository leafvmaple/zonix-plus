#pragma once
#include "minilib.h"

/* Shared command I/O policy; startup, syscalls and libc primitives stay in the runtime. */
static inline long command_write_all(int fd, const void* buffer, long size) {
    const char* bytes = (const char*)buffer;
    long done = 0;
    while (done < size) {
        long written = sys_write(fd, (void*)(bytes + done), size - done);
        if (written <= 0)
            return SYS_ERROR_IO;
        done += written;
    }
    return 0;
}

static inline long command_text(int fd, const char* text) {
    return command_write_all(fd, text, strlen((char*)text));
}

static inline const char* command_error_text(long error) {
    switch (error) {
        case SYS_ERROR_IO: return "I/O error";
        case SYS_ERROR_NOT_FOUND: return "not found";
        case SYS_ERROR_EXISTS: return "already exists";
        case SYS_ERROR_NOT_EMPTY: return "directory not empty";
        case SYS_ERROR_NO_DEVICE: return "device unavailable";
        case SYS_ERROR_BUSY: return "resource busy";
        case SYS_ERROR_ACCESS: return "permission denied";
        case SYS_ERROR_NOT_SUPPORTED: return "operation not supported";
        case SYS_ERROR_FULL: return "limit exceeded";
        case SYS_ERROR_INVALID: return "invalid argument";
        case SYS_ERROR_INTERRUPTED: return "interrupted";
        default: return "operation failed";
    }
}

static inline int command_error(const char* command, const char* path, long error) {
    command_text(STDERR_FD, command);
    command_text(STDERR_FD, ": ");
    if (path) {
        command_text(STDERR_FD, path);
        command_text(STDERR_FD, ": ");
    }
    command_text(STDERR_FD, command_error_text(error));
    command_text(STDERR_FD, "\n");
    return 1;
}

static inline int command_usage(const char* text) {
    command_text(STDERR_FD, text);
    return 2;
}

static inline long command_copy(int input, int output) {
    char buffer[4096];
    for (;;) {
        long count = sys_read(input, buffer, sizeof(buffer));
        if (count <= 0)
            return count;
        long error = command_write_all(output, buffer, count);
        if (error < 0)
            return error;
    }
}

/* CLI line policy over byte input: positive length includes Enter's newline;
 * zero is EOF, negative is an Error. Echo uses stderr so stdout can be
 * redirected. Ctrl-D delivers pending text before reporting EOF. */
static inline long command_read_line(char* line, long capacity, int* skip_lf) {
    long size = 0;
    int overflow = 0;
    for (;;) {
        char c;
        long count = sys_read(STDIN_FD, &c, 1);
        if (count < 0)
            return SYS_ERROR_IO;
        if (count == 0) {
            line[size] = 0;
            return overflow ? SYS_ERROR_FULL : size;
        }
        if (*skip_lf && c == '\n') {
            *skip_lf = 0;
            continue;
        }
        *skip_lf = 0;
        if (c == 3) {
            line[0] = 0;
            if (command_text(STDERR_FD, "^C\n") < 0)
                return SYS_ERROR_IO;
            return SYS_ERROR_INTERRUPTED;
        }
        if (c == 4) {
            line[size] = 0;
            return overflow ? SYS_ERROR_FULL : size;
        }
        if (c == '\r' || c == '\n') {
            *skip_lf = c == '\r';
            if (command_text(STDERR_FD, "\n") < 0)
                return SYS_ERROR_IO;
            if (overflow)
                return SYS_ERROR_FULL;
            line[size++] = '\n';
            line[size] = 0;
            return size;
        }
        if ((c == 8 || c == 127) && !overflow) {
            if (size > 0) {
                --size;
                if (command_text(STDERR_FD, "\b \b") < 0)
                    return SYS_ERROR_IO;
            }
        } else if (c >= 32 || c == '\t') {
            if (size + 2 >= capacity)
                overflow = 1;
            else if (!overflow) {
                line[size++] = c;
                if (command_write_all(STDERR_FD, &c, 1) < 0)
                    return SYS_ERROR_IO;
            }
        }
    }
}

/* Adds one record to the kernel log, which logd keeps in /var/log/kernel.log.
 * priority: 3 error, 4 warning, 6 information, 7 debug. Failure is ignored. */
static inline void command_log(int priority, const char* text) {
    char line[256];
    long size = 0;
    line[size++] = '<';
    line[size++] = (char)('0' + (priority & 7));
    line[size++] = '>';
    while (*text && size < (long)sizeof(line))
        line[size++] = *text++;
    int fd = sys_open((char*)"/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    sys_write(fd, line, size);
    sys_close(fd);
}
