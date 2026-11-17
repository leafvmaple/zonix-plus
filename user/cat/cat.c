#include "../command.h"

int main(int argc, char** argv) {
    if (argc < 2)
        return command_usage("Usage: cat file...\n");
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        struct FileStat st;
        long error = sys_stat(argv[i], &st);
        if (error < 0 || st.type != FILE_TYPE_FILE) {
            status = command_error("cat", argv[i], error < 0 ? error : SYS_ERROR_INVALID);
            continue;
        }
        int fd = sys_open(argv[i], O_RDONLY);
        if (fd < 0) {
            status = command_error("cat", argv[i], fd);
            continue;
        }
        error = command_copy(fd, STDOUT_FD);
        if (error < 0)
            status = command_error("cat", argv[i], error);
        if (sys_close(fd) < 0)
            status = command_error("cat", argv[i], SYS_ERROR_IO);
    }
    return status;
}
