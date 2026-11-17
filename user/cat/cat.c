#include "../command.h"

int main(int argc, char** argv) {
    if (argc == 1) {
        struct DescriptorStat input;
        long error = sys_fstat(STDIN_FD, &input);
        if (error < 0)
            return command_error("cat", "stdin", error);
        if (input.type != FILE_TYPE_CHAR_DEVICE) {
            error = command_copy(STDIN_FD, STDOUT_FD);
            return error < 0 ? command_error("cat", "stdin", error) : 0;
        }
        char line[4096];
        int skip_lf = 0;
        for (;;) {
            long size = command_read_line(line, sizeof(line), &skip_lf);
            if (size == 0)
                return 0;
            if (size > 0)
                size = command_write_all(STDOUT_FD, line, size);
            if (size < 0)
                return command_error("cat", 0, size);
        }
    }
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
