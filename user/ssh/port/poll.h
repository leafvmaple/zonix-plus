#pragma once

#define POLLIN  1
#define POLLOUT 2
#define POLLERR 4

struct pollfd {
    int fd;
    short events;
    short revents;
};

int poll(struct pollfd* entries, unsigned long count, int timeout_ms);
