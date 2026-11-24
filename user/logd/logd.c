#include "../command.h"

/* Copies the kernel log from /dev/kmsg into /var/log/kernel.log so that it
 * survives a crash or a power cut. Records are on disk within SYNC_MS, and at
 * once for warnings and errors. Past LOG_LIMIT the file becomes kernel.old. */

#define LOG_PATH   "/var/log/kernel.log"
#define OLD_PATH   "/var/log/kernel.old"
#define OLD_NAME   "kernel.old"
#define LOG_LIMIT  (1024L * 1024L)
#define SYNC_MS    1000
#define READ_BYTES 4096

static long file_size(char* path) {
    struct FileStat stat;
    return sys_stat(path, &stat) < 0 ? 0 : (long)stat.size;
}

/* Opens the log for appending, first moving a full one aside. */
static int open_log(long* size) {
    *size = file_size(LOG_PATH);
    if (*size >= LOG_LIMIT) {
        sys_unlink(OLD_PATH);
        sys_rename(LOG_PATH, OLD_NAME);
        *size = 0;
    }
    return sys_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC);
}

static int urgent(const char* text, long length) {
    static const char* const marks[] = {"] [warn] ", "] [error] "};
    for (int m = 0; m < 2; ++m) {
        long mark_length = (long)strlen((char*)marks[m]);
        for (long i = 0; i + mark_length <= length; ++i)
            if (memcmp((void*)(text + i), (void*)marks[m], mark_length) == 0)
                return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    (void)argv;
    if (argc != 1)
        return command_usage("Usage: logd\n");
    sys_mkdir("/var");
    sys_mkdir("/var/log");
    int kmsg = sys_open("/dev/kmsg", O_RDONLY | O_CLOEXEC);
    if (kmsg < 0)
        return command_error("logd", "/dev/kmsg", kmsg);
    long size;
    int log = open_log(&size);
    if (log < 0)
        return command_error("logd", LOG_PATH, log);

    static char buffer[READ_BYTES];
    /* A marker between boots: UTC seconds since 1970, or 0 without a clock. */
    long now = sys_realtime();
    const char* header = "---- boot, UTC time ";
    command_write_all(log, header, (long)strlen((char*)header));
    char digits[24];
    int digit_count = 0;
    unsigned long value = now >= 0 ? (unsigned long)now : 0;
    do {
        digits[digit_count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && digit_count < 20);
    for (int i = digit_count - 1; i >= 0; --i)
        command_write_all(log, &digits[i], 1);
    command_write_all(log, " ----\n", 6);

    int dirty = 1;
    long last_sync = 0;
    for (;;) {
        long ready = dirty ? sys_wait_readable_for(1L << kmsg, SYNC_MS) : sys_wait_readable(1L << kmsg);
        if (ready < 0)
            return 1;
        if (ready > 0) {
            long count = sys_read(kmsg, buffer, sizeof(buffer));
            if (count <= 0)
                return 1;
            if (command_write_all(log, buffer, count) < 0)
                return command_error("logd", LOG_PATH, SYS_ERROR_IO);
            size += count;
            dirty = 1;
            if (!urgent(buffer, count) && sys_uptime_ms() - last_sync < SYNC_MS)
                continue;
        }
        if (dirty) {
            sys_sync();
            last_sync = sys_uptime_ms();
            dirty = 0;
        }
        if (size >= LOG_LIMIT) {
            sys_close(log);
            log = open_log(&size);
            if (log < 0)
                return command_error("logd", LOG_PATH, log);
        }
    }
}
