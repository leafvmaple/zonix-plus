#include "zonix_port.h"
#include "../command.h"
#include "../crypto/zonix_crypto.h"
#include "crypto_hash_sha256.h"
#include "e.h"
#include "randombytes.h"
#include "global.h"
#include "trymlock.h"
#include <mbedtls/sha256.h>
#include <poll.h>
#include <unistd.h>

#define FD_MASK_BITS   16
#define IO_WRITE_BYTES 1024

int errno;
static long deadline_ms;
static int tty_pending_lf;

void zonix_deadline(long milliseconds) {
    deadline_ms = sys_uptime_ms() + milliseconds;
}

static int deadline_ok(void) {
    if (sys_uptime_ms() < deadline_ms)
        return 1;
    errno = ETIMEDOUT;
    return 0;
}

int poll(struct pollfd* entries, unsigned long count, int timeout_ms) {
    if (!deadline_ok())
        return -1;
    long remaining = deadline_ms - sys_uptime_ms();
    if (remaining < 0)
        remaining = 0;
    if (timeout_ms >= 0 && timeout_ms < remaining)
        remaining = timeout_ms;

    unsigned int reading = 0;
    unsigned int writing = 0;
    for (unsigned long i = 0; i < count; ++i) {
        struct pollfd* entry = &entries[i];
        entry->revents = 0;
        if (entry->fd < 0 || entry->fd >= FD_MASK_BITS) {
            errno = EBADF;
            return -1;
        }
        unsigned int bit = 1U << entry->fd;
        if ((entry->events & POLLIN) != 0)
            reading |= bit;
        if ((entry->events & POLLOUT) != 0)
            writing |= bit;
    }
    long ready = sys_wait_io(reading, writing, remaining);
    if (ready < 0) {
        errno = EIO;
        return -1;
    }
    if (!deadline_ok())
        return -1;
    int ready_count = 0;
    for (unsigned long i = 0; i < count; ++i) {
        struct pollfd* entry = &entries[i];
        unsigned long bit = 1UL << entry->fd;
        if (((unsigned long)ready & bit) != 0 && (entry->events & POLLIN) != 0)
            entry->revents |= POLLIN;
        if (((unsigned long)ready & (bit << FD_MASK_BITS)) != 0 && (entry->events & POLLOUT) != 0)
            entry->revents |= POLLOUT;
        if (entry->revents != 0)
            ++ready_count;
    }
    return ready_count;
}

long read(int fd, void* output, size_t size) {
    if (!deadline_ok())
        return -1;
    if (size == 0)
        return 0;
    long ready = sys_wait_io(1U << fd, 0, 0);
    if (ready <= 0) {
        errno = ready == 0 ? EAGAIN : EIO;
        return -1;
    }
    long result = sys_read(fd, output, (long)size);
    if (result < 0)
        errno = EIO;
    return result;
}

long write(int fd, const void* data, size_t size) {
    if (size == 0)
        return 0;
    if (fd == STDERR_FD) {
        long result = sys_write(fd, (void*)data, (long)size);
        if (result < 0)
            errno = EIO;
        return result;
    }
    if (!deadline_ok())
        return -1;
    long ready = sys_wait_io(0, 1U << fd, 0);
    if (ready <= 0) {
        errno = ready == 0 ? EAGAIN : EIO;
        return -1;
    }
    if (size > IO_WRITE_BYTES)
        size = IO_WRITE_BYTES;
    for (;;) {
        long result = sys_write(fd, (void*)data, (long)size);
        if (result >= 0)
            return result;
        if (size == 1)
            break;
        size /= 2;
    }
    errno = EIO;
    return -1;
}

int close(int fd) {
    return sys_close(fd);
}

_Noreturn void _exit(int status) {
    sys_exit(status);
    for (;;)
        sys_yield();
}

long zonix_spawn_shell(int slave) {
    int copies[3];
    for (int fd = 0; fd < 3; ++fd)
        copies[fd] = (int)sys_fcntl(fd, F_DUPFD_CLOEXEC, 3);
    long pid = SYS_ERROR_IO;
    if (copies[0] >= 0 && copies[1] >= 0 && copies[2] >= 0) {
        if (sys_dup2(slave, STDIN_FD) >= 0 && sys_dup2(slave, STDOUT_FD) >= 0 && sys_dup2(slave, STDERR_FD) >= 0) {
            char path[] = "/bin/sh";
            char* argv[] = {path, 0};
            pid = sys_spawn(path, 1, argv);
        }
        for (int fd = 0; fd < 3; ++fd)
            if (sys_dup2(copies[fd], fd) < 0)
                global_die(111);
    }
    for (int fd = 0; fd < 3; ++fd)
        if (copies[fd] >= 0)
            sys_close(copies[fd]);
    return pid;
}

int zonix_pty_read_pending(void) {
    return tty_pending_lf;
}

long zonix_pty_read(int master, unsigned char* output, long size) {
    static int preceding_cr;
    if (tty_pending_lf) {
        tty_pending_lf = 0;
        output[0] = '\n';
        return 1;
    }
    unsigned char bytes[IO_WRITE_BYTES];
    size_t limit = (size_t)(size > 1 ? size / 2 : 1);
    if (limit > sizeof(bytes))
        limit = sizeof(bytes);
    long count = read(master, bytes, limit);
    if (count <= 0)
        return count;
    long written = 0;
    for (long i = 0; i < count; ++i) {
        unsigned char byte = bytes[i];
        if (byte == '\n' && !preceding_cr) {
            output[written++] = '\r';
            if (written == size) {
                tty_pending_lf = 1;
                break;
            }
        }
        output[written++] = byte;
        preceding_cr = byte == '\r';
    }
    return written;
}

long zonix_pty_write(int master, const unsigned char* data, long size) {
    // The PTY treats a standalone Ctrl-C as a foreground interrupt.
    if (size > 0 && data[0] == 3) {
        long result = sys_write(master, (void*)data, 1);
        if (result < 0)
            errno = EIO;
        return result;
    }
    const long limit = size < IO_WRITE_BYTES ? size : IO_WRITE_BYTES;
    for (long i = 1; i < limit; ++i)
        if (data[i] == 3)
            return write(master, data, (size_t)i);
    return write(master, data, (size_t)limit);
}

int zonix_load_key(const char* path, void* output, long size) {
    int fd = sys_open((char*)path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    struct DescriptorStat stat;
    int ok = sys_fstat(fd, &stat) == 0 && stat.type == FILE_TYPE_FILE && stat.size == (unsigned long)size &&
             sys_read(fd, output, size) == size;
    sys_close(fd);
    return ok;
}

void randombytes(void* buffer, long long size) {
    if (size < 0 || zonix_crypto_random(buffer, (size_t)size) != 0) {
        command_text(STDERR_FD, "sshd: system random source unavailable\n");
        global_die(111);
    }
}

int crypto_hash_sha256_zonix(unsigned char* digest, const unsigned char* data, unsigned long long size) {
    return mbedtls_sha256(data, (size_t)size, digest, 0);
}

// The login uses a finite lifetime and wipes key buffers. Zonix has no mlock ABI.
void trymlock(void* data, long long size) {
    (void)data;
    (void)size;
}

void trymunlock(void* data, long long size) {
    (void)data;
    (void)size;
}
