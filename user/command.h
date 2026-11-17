#pragma once
#include "minilib.h"

/* Shared command I/O policy; startup, syscalls and libc primitives stay in the runtime. */
static inline long command_write_all(int fd, const void* buffer, long size) {
    const char* bytes = buffer;
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
