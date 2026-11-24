#include "../command.h"

/* Prints the kernel log records still in the kernel's ring, then exits.
 * With an argument, prints only the lines that contain it. */

#define READ_BYTES 4096

static int contains(const char* line, long length, const char* text) {
    long wanted = (long)strlen((char*)text);
    for (long start = 0; start + wanted <= length; ++start) {
        long i = 0;
        while (i < wanted && line[start + i] == text[i])
            ++i;
        if (i == wanted)
            return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 2)
        return command_usage("Usage: dmesg [text]\n");
    const char* filter = argc == 2 ? argv[1] : 0;
    int kmsg = sys_open("/dev/kmsg", O_RDONLY | O_CLOEXEC);
    if (kmsg < 0)
        return command_error("dmesg", "/dev/kmsg", kmsg);
    long error = sys_fcntl(kmsg, F_SETFL, O_NONBLOCK);
    if (error < 0)
        return command_error("dmesg", "/dev/kmsg", error);
    static char buffer[READ_BYTES];
    for (;;) {
        /* Each read returns whole lines; a nonblocking read at the end fails. */
        long count = sys_read(kmsg, buffer, sizeof(buffer));
        if (count <= 0)
            break;
        long start = 0;
        for (long i = 0; i < count; ++i) {
            if (buffer[i] != '\n')
                continue;
            long length = i + 1 - start;
            if (!filter || contains(buffer + start, length, filter))
                command_write_all(1, buffer + start, length);
            start = i + 1;
        }
    }
    sys_close(kmsg);
    return 0;
}
