/*
 * Zonix OS — Syscall ABI Definitions
 *
 * This header is the single source of truth for syscall numbers shared
 * between the kernel and user-space toolchains (e.g. zcc runtime).
 *
 * Rules:
 *   - Pure C preprocessor macros only (no C++ / no typedefs).
 *   - Must be includable from .S assembly files via #include.
 *   - Keep sorted by syscall number.
 */

#ifndef ZONIX_ABI_SYSCALL_H
#define ZONIX_ABI_SYSCALL_H

/* ---- Syscall numbers ---- */
#define NR_EXIT      1
#define NR_READ      3
#define NR_WRITE     4
#define NR_OPEN      5
#define NR_CLOSE     6
#define NR_WAIT      7
#define NR_UNLINK    10
#define NR_CHDIR     12
#define NR_GETPID    20
#define NR_PAUSE     29
#define NR_SYNC      36
#define NR_KILL      37
#define NR_RENAME    38
#define NR_MKDIR     39
#define NR_RMDIR     40
#define NR_IOCTL     54
#define NR_FCNTL     55
#define NR_DUP2      63
#define NR_READDIR   89
#define NR_FTRUNCATE 93
#define NR_STAT      106
#define NR_FSTAT     108
#define NR_YIELD     158
#define NR_GETCWD    183
#define NR_PIPE2     331
#define NR_SPAWN     400
#define NR_SHUTDOWN  401
#define NR_SPAWN_FOREGROUND 402
#define NR_PROCESS_NEXT 403
#define NR_WAIT_READABLE 404
#define NR_OPENPTY 405
#define NR_UPTIME_MS 406
#define NR_WAIT_READABLE_FOR 407
#define NR_GETRANDOM 408
#define NR_WAIT_IO 409
#define NR_REALTIME 410
#define NR_BRK 411

/* ---- Stdout / Stderr fd constants ---- */
#define STDIN_FD  0
#define STDOUT_FD 1
#define STDERR_FD 2

#endif /* ZONIX_ABI_SYSCALL_H */
