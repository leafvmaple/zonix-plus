#include "../command.h"

/* Renames a file or directory. The kernel renames an entry inside its own
 * directory, so the target must name the same directory as the source, or be
 * a bare name. Changing only the case ("VAR" to "var") is a rename too. */

/* Length of the directory part of `path`, including its last '/'. */
static long directory_length(const char* path) {
    long length = 0;
    for (long i = 0; path[i]; ++i)
        if (path[i] == '/')
            length = i + 1;
    return length;
}

int main(int argc, char** argv) {
    if (argc != 3)
        return command_usage("Usage: mv source name\n");
    char* source = argv[1];
    char* target = argv[2];
    long source_dir = directory_length(source);
    long target_dir = directory_length(target);
    char* name = target + target_dir;
    if (*name == 0 || source[source_dir] == 0)
        return command_usage("Usage: mv source name\n");
    if (target_dir != 0 && (target_dir != source_dir || memcmp(source, target, target_dir) != 0)) {
        command_text(STDERR_FD, "mv: can only rename within one directory\n");
        return 1;
    }
    long error = sys_rename(source, name);
    return error < 0 ? command_error("mv", source, error) : 0;
}
