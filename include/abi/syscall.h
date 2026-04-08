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

#ifndef _ZONIX_ABI_SYSCALL_H
#define _ZONIX_ABI_SYSCALL_H

/* ---- Syscall numbers ---- */
#define NR_EXIT     1
#define NR_READ     3
#define NR_WRITE    4
#define NR_OPEN     5
#define NR_CLOSE    6
#define NR_PAUSE    29

/* ---- Stdout / Stderr fd constants ---- */
#define STDIN_FD    0
#define STDOUT_FD   1
#define STDERR_FD   2

#endif /* _ZONIX_ABI_SYSCALL_H */
