#pragma once

/* Spawn arguments include argv[0]; argc=0 with argv=NULL is also valid. */
#define PROCESS_MAX_ARGS      16
#define PROCESS_MAX_ARG_BYTES 4096
#define PROCESS_MAX_PATH      128
#define PROCESS_STATUS_KILLED (-128)

/* New process/metadata calls return negative Error values; legacy file calls use -1. */
#define SYS_ERROR_INVALID     (-3)
#define SYS_ERROR_NOT_FOUND   (-4)
#define SYS_ERROR_ACCESS      (-14)
#define SYS_ERROR_FULL        (-12)
#define SYS_ERROR_INTERRUPTED (-15)

#define FILE_TYPE_FILE        1
#define FILE_TYPE_DIRECTORY   2
#define FILE_TYPE_CHAR_DEVICE 3

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
