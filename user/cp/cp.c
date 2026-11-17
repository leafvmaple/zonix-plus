#include "../command.h"

int main(int argc, char** argv) {
    if (argc != 3)
        return command_usage("Usage: cp source destination\n");
    char target[PROCESS_MAX_PATH];
    char* destination = argv[2];
    struct FileStat path_stat;
    long error = sys_stat(destination, &path_stat);
    if (error == 0 && path_stat.type == FILE_TYPE_DIRECTORY) {
        char* name = argv[1];
        for (char* p = name; *p; ++p) {
            if (*p == '/')
                name = p + 1;
        }
        long prefix = strlen(destination);
        long size = strlen(name);
        if (!size || prefix + size + 2 > PROCESS_MAX_PATH)
            return command_error("cp", destination, SYS_ERROR_FULL);
        memcpy(target, destination, prefix);
        target[prefix] = '/';
        memcpy(target + prefix + 1, name, size + 1);
        destination = target;
    } else if (error < 0 && error != SYS_ERROR_NOT_FOUND) {
        return command_error("cp", destination, error);
    }

    int input = sys_open(argv[1], O_RDONLY);
    if (input < 0)
        return command_error("cp", argv[1], input);
    struct DescriptorStat source;
    error = sys_fstat(input, &source);
    if (error < 0 || source.type != FILE_TYPE_FILE || !source.device || !source.inode) {
        int status = command_error("cp", argv[1], error < 0 ? error : SYS_ERROR_NOT_SUPPORTED);
        if (sys_close(input) < 0)
            status = command_error("cp", argv[1], SYS_ERROR_IO);
        return status;
    }

    /* Open without truncating, compare actual open-file identities, then truncate. */
    int output = sys_open(destination, O_WRONLY | O_CREAT);
    if (output < 0) {
        int status = command_error("cp", destination, output);
        if (sys_close(input) < 0)
            status = command_error("cp", argv[1], SYS_ERROR_IO);
        return status;
    }
    struct DescriptorStat dest;
    error = sys_fstat(output, &dest);
    int status = 0;
    if (error < 0 || dest.type != FILE_TYPE_FILE || !dest.device || !dest.inode) {
        status = command_error("cp", destination, error < 0 ? error : SYS_ERROR_NOT_SUPPORTED);
    } else if (source.device == dest.device && source.inode == dest.inode) {
        command_text(STDERR_FD, "cp: source and destination are the same file\n");
        status = 1;
    } else {
        error = sys_ftruncate(output, 0);
        if (error == 0)
            error = command_copy(input, output);
        if (error < 0)
            status = command_error("cp", destination, error);
    }
    if (sys_close(output) < 0)
        status = command_error("cp", destination, SYS_ERROR_IO);
    if (sys_close(input) < 0)
        status = command_error("cp", argv[1], SYS_ERROR_IO);
    return status;
}
