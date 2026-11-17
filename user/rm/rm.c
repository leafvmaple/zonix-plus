#include "../command.h"

int main(int argc, char** argv) {
    int directories = argc > 1 && !strcmp(argv[1], "-d");
    int first = directories ? 2 : 1;
    if (argc <= first)
        return command_usage("Usage: rm [-d] path...\n");
    int status = 0;
    for (int i = first; i < argc; ++i) {
        long error;
        if (directories) {
            struct FileStat st;
            error = sys_stat(argv[i], &st);
            if (error == 0)
                error = st.type == FILE_TYPE_DIRECTORY ? sys_rmdir(argv[i]) : sys_unlink(argv[i]);
        } else {
            error = sys_unlink(argv[i]);
        }
        if (error < 0)
            status = command_error("rm", argv[i], error);
    }
    return status;
}
