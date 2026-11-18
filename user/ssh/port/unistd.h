#pragma once

#include <stddef.h>

long read(int fd, void* out, size_t size);
long write(int fd, const void* data, size_t size);
int close(int fd);
_Noreturn void _exit(int status);
