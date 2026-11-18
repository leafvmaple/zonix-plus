#pragma once

/* Spawn arguments include argv[0]; argc=0 with argv=NULL is also valid. */
#define PROCESS_MAX_ARGS      16
#define PROCESS_MAX_ARG_BYTES 4096
#define PROCESS_MAX_PATH      128
#define PROCESS_STATUS_KILLED (-128)

#define PROCESS_NAME_CAPACITY 32
#define PROCESS_STATE_UNINIT   0
#define PROCESS_STATE_SLEEPING 1
#define PROCESS_STATE_RUNNABLE 2
#define PROCESS_STATE_RUNNING  3
#define PROCESS_STATE_ZOMBIE   4
#define PROCESS_KIND_USER   1
#define PROCESS_KIND_KERNEL 2

/* process_next(-1, ...) starts enumeration, including PID 0. Each call
 * returns the lowest PID greater than the cursor: 1=entry, 0=end. */
struct ProcessInfo {
    int pid;
    int ppid;
    unsigned int state;
    unsigned int kind;
    char name[PROCESS_NAME_CAPACITY];
};

/* New process/metadata calls return negative Error values; legacy file calls use -1. */
#define SYS_ERROR_IO            (-1)
#define SYS_ERROR_EXISTS        (-6)
#define SYS_ERROR_NOT_EMPTY     (-7)
#define SYS_ERROR_BUSY          (-9)
#define SYS_ERROR_NOT_SUPPORTED (-11)
#define SYS_ERROR_INVALID       (-3)
#define SYS_ERROR_NOT_FOUND     (-4)
#define SYS_ERROR_ACCESS        (-14)
#define SYS_ERROR_FULL          (-12)
#define SYS_ERROR_INTERRUPTED   (-15)

#define FILE_TYPE_FILE        1
#define FILE_TYPE_DIRECTORY   2
#define FILE_TYPE_CHAR_DEVICE 3
#define FILE_TYPE_PIPE        4

struct FileStat {
    unsigned int type;
    unsigned int size;
    unsigned int attrs;
};

struct DirectoryEntry {
    char name[32];
    unsigned int type;
    unsigned int size;
    unsigned int attrs;
};

/* FD metadata. Nonzero device/inode identify a file while descriptors remain open.
 * Zero identity means the backend cannot identify the object. stat's FileStat ABI
 * remains unchanged. ftruncate currently supports length=0 only. */
struct DescriptorStat {
    unsigned int type;
    unsigned int size;
    unsigned int attrs;
    unsigned int reserved;
    long device;
    long inode;
};
