#pragma once

#include "minilib.h"
#include <stddef.h>

void zonix_deadline(long milliseconds);
long zonix_spawn_shell(int slave);
int zonix_pty_read_pending(void);
long zonix_pty_read(int master, unsigned char* output, long size);
long zonix_pty_write(int master, const unsigned char* data, long size);
int zonix_load_key(const char* path, void* output, long size);
