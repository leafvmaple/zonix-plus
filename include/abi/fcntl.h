/* Open flags shared by the kernel and user programs; usable from C and assembly. */
#ifndef ZONIX_ABI_FCNTL_H
#define ZONIX_ABI_FCNTL_H

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_ACCMODE 3
#define O_CREAT   0100
#define O_EXCL    0200
#define O_TRUNC   01000
#define O_NONBLOCK 04000
#define O_APPEND  02000
#define O_CLOEXEC 02000000

/* Descriptor duplication: lowest free FD >= argument, closed on image spawn. */
#define F_DUPFD_CLOEXEC 1030
#define F_SETFL 4

/* open's mode argument is reserved and must be zero; FAT has no POSIX mode bits. */

#endif
