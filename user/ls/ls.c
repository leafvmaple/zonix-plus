#include "../command.h"

int main(int argc, char** argv) {
    if (argc > 2)
        return command_usage("Usage: ls [path]\n");
    char* path = argc == 2 ? argv[1] : ".";
    struct FileStat st;
    long error = sys_stat(path, &st);
    if (error < 0)
        return command_error("ls", path, error);
    if (st.type != FILE_TYPE_DIRECTORY) {
        if (command_text(STDOUT_FD, path) < 0 || command_text(STDOUT_FD, "\n") < 0)
            return command_error("ls", 0, SYS_ERROR_IO);
        return 0;
    }
    for (long i = 0;; ++i) {
        struct DirectoryEntry entry;
        long count = sys_readdir(path, i, &entry);
        if (count < 0)
            return command_error("ls", path, count);
        if (count == 0)
            return 0;
        if (!strcmp(entry.name, ".") || !strcmp(entry.name, ".."))
            continue;
        if (command_text(STDOUT_FD, entry.name) < 0 ||
            (entry.type == FILE_TYPE_DIRECTORY && command_text(STDOUT_FD, "/") < 0) ||
            command_text(STDOUT_FD, "\n") < 0)
            return command_error("ls", 0, SYS_ERROR_IO);
    }
}
