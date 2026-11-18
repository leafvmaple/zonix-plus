#pragma once

#include <minilib.h>

namespace desktop_ipc {

class Fd {
public:
    explicit Fd(long fd) : fd_(static_cast<int>(fd)) {}
    ~Fd() {
        if (fd_ >= 0)
            sys_close(fd_);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
    int release() {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};

}  // namespace desktop_ipc
