#include "cmd.h"

#include "cons/cons.h"
#include "fs/vfs.h"
#include "lib/memory.h"
#include "lib/result.h"
#include "lib/stdio.h"
#include "lib/string.h"

#include <base/bpb.h>

namespace cmd {

class LsVisitor : public vfs::DirVisitor {
public:
    int visit(const vfs::DirEntry& entry) override {
        char attr_str[6] = "-----";
        if (entry.attrs & FAT_ATTR_DIRECTORY)
            attr_str[0] = 'd';
        if (entry.attrs & FAT_ATTR_READ_ONLY)
            attr_str[1] = 'r';
        if (entry.attrs & FAT_ATTR_HIDDEN)
            attr_str[2] = 'h';
        if (entry.attrs & FAT_ATTR_SYSTEM)
            attr_str[3] = 's';
        if (entry.attrs & FAT_ATTR_ARCHIVE)
            attr_str[4] = 'a';

        cprintf("%s %8d  %s\n", attr_str, entry.size, entry.name);
        return 0;
    }
};

static void cmd_ls(int argc, char** argv) {
    int use_mnt = 0;
    if (argc >= 2 && strcmp(argv[1], "/mnt") == 0) {
        use_mnt = 1;
    }

    const char* path = nullptr;

    if (use_mnt) {
        if (!mnt_mounted()) {
            cprintf("Nothing mounted at /mnt\n");
            cprintf("Use 'mount <device>' to mount a device\n");
            return;
        }
        path = "/mnt";
    } else {
        if (!vfs::is_mounted("/")) {
            cprintf("Error: no system disk mounted at /\n");
            return;
        }
        path = "/";
    }

    cprintf("Directory listing of %s:\n", path);
    cprintf("ATTR     SIZE     NAME\n");
    cprintf("-------- -------- ------------\n");

    LsVisitor visitor;
    auto count = vfs::readdir(path, visitor);

    if (!count.ok()) {
        cprintf("Failed to read directory\n");
    } else {
        cprintf("\nTotal: %d file(s)\n", count.value());
    }
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: cat <filename> [/mnt]\n");
        cprintf("  cat <filename>     - read file from system disk (/)\n");
        cprintf("  cat <filename> /mnt - read file from mounted disk (/mnt)\n");
        return;
    }

    const char* filename = argv[1];

    int use_mnt = 0;
    if (argc >= 3 && strcmp(argv[2], "/mnt") == 0) {
        use_mnt = 1;
    }

    if (use_mnt) {
        if (!mnt_mounted()) {
            cprintf("Nothing mounted at /mnt\n");
            return;
        }
    } else {
        if (!vfs::is_mounted("/")) {
            cprintf("Error: no system disk mounted at /\n");
            return;
        }
    }

    char path_buf[PATH_BUF_SIZE]{};
    if (build_path(filename, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long: %s\n", filename);
        return;
    }

    vfs::File* file = nullptr;
    if (vfs::open(path_buf, &file) != Error::None || !file) {
        cprintf("File not found: %s\n", filename);
        return;
    }

    vfs::Stat st{};
    if (file->stat(&st) != Error::None) {
        vfs::close(file);
        cprintf("Failed to stat file: %s\n", filename);
        return;
    }

    if (st.type != vfs::NodeType::File) {
        vfs::close(file);
        cprintf("Cannot cat a directory\n");
        return;
    }

    uint32_t max_size = 65536;
    uint32_t size = st.size;
    if (size > max_size) {
        cprintf("File too large (max %d bytes)\n", max_size);
        size = max_size;
    }

    if (size == 0) {
        vfs::close(file);
        cprintf("(empty file)\n");
        return;
    }

    static uint8_t file_buf[4096];
    uint32_t offset = 0;

    cprintf("--- File: %s (%d bytes) ---\n", filename, st.size);

    while (offset < size) {
        uint32_t chunk_size = size - offset;
        if (chunk_size > sizeof(file_buf)) {
            chunk_size = sizeof(file_buf);
        }

        auto rd = vfs::read(file, file_buf, chunk_size, offset);
        if (!rd.ok() || rd.value() <= 0) {
            cprintf("\nError reading file at offset %d\n", offset);
            break;
        }
        int read = rd.value();

        for (int i = 0; i < read; i++) {
            char c = file_buf[i];
            if (c == '\n') {
                cons::putc('\n');
            } else if (c == '\r') {
                // Skip CR
            } else if (c >= 32 && c < 127) {
                cons::putc(c);
            } else {
                cons::putc('.');
            }
        }

        offset += read;
    }

    vfs::close(file);
    cprintf("\n--- End of file ---\n");
}

static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: mkdir <dirname> [/mnt]\n");
        return;
    }

    const char* dirname = argv[1];
    int use_mnt = (argc >= 3 && strcmp(argv[2], "/mnt") == 0);

    if (use_mnt && !mnt_mounted()) {
        cprintf("Nothing mounted at /mnt\n");
        return;
    }

    if (!use_mnt && !vfs::is_mounted("/")) {
        cprintf("Error: no system disk mounted at /\n");
        return;
    }

    char path_buf[PATH_BUF_SIZE]{};
    if (build_path(dirname, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long\n");
        return;
    }

    if (vfs::mkdir(path_buf) != Error::None) {
        cprintf("Failed to create directory: %s\n", dirname);
    }
}

static void cmd_touch(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: touch <filename> [/mnt]\n");
        return;
    }

    const char* filename = argv[1];
    int use_mnt = (argc >= 3 && strcmp(argv[2], "/mnt") == 0);

    if (use_mnt && !mnt_mounted()) {
        cprintf("Nothing mounted at /mnt\n");
        return;
    }

    if (!use_mnt && !vfs::is_mounted("/")) {
        cprintf("Error: no system disk mounted at /\n");
        return;
    }

    char path_buf[PATH_BUF_SIZE]{};
    if (build_path(filename, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long\n");
        return;
    }

    if (vfs::create(path_buf) != Error::None) {
        cprintf("Failed to create file: %s\n", filename);
    }
}

static void cmd_rm(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: rm <filename> [/mnt]\n");
        return;
    }

    const char* filename = argv[1];
    int use_mnt = (argc >= 3 && strcmp(argv[2], "/mnt") == 0);

    if (use_mnt && !mnt_mounted()) {
        cprintf("Nothing mounted at /mnt\n");
        return;
    }

    if (!use_mnt && !vfs::is_mounted("/")) {
        cprintf("Error: no system disk mounted at /\n");
        return;
    }

    char path_buf[PATH_BUF_SIZE]{};
    if (build_path(filename, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long\n");
        return;
    }

    if (vfs::unlink(path_buf) != Error::None) {
        cprintf("Failed to remove file: %s\n", filename);
    }
}

static void cmd_rmdir(int argc, char** argv) {
    if (argc < 2) {
        cprintf("Usage: rmdir <dirname> [/mnt]\n");
        return;
    }

    const char* dirname = argv[1];
    int use_mnt = (argc >= 3 && strcmp(argv[2], "/mnt") == 0);

    if (use_mnt && !mnt_mounted()) {
        cprintf("Nothing mounted at /mnt\n");
        return;
    }

    if (!use_mnt && !vfs::is_mounted("/")) {
        cprintf("Error: no system disk mounted at /\n");
        return;
    }

    char path_buf[PATH_BUF_SIZE]{};
    if (build_path(dirname, use_mnt, path_buf, sizeof(path_buf)) != 0) {
        cprintf("Path too long\n");
        return;
    }

    if (vfs::rmdir(path_buf) != Error::None) {
        cprintf("Failed to remove directory: %s\n", dirname);
    }
}

void register_fs_commands() {
    shell::register_command("ls", "List files (usage: ls [/mnt])", cmd_ls);
    shell::register_command("cat", "Display file contents (usage: cat <file> [/mnt])", cmd_cat);
    shell::register_command("mkdir", "Create directory (usage: mkdir <dir> [/mnt])", cmd_mkdir);
    shell::register_command("touch", "Create empty file (usage: touch <file> [/mnt])", cmd_touch);
    shell::register_command("rm", "Remove file (usage: rm <file> [/mnt])", cmd_rm);
    shell::register_command("rmdir", "Remove empty directory (usage: rmdir <dir> [/mnt])", cmd_rmdir);
}

}  // namespace cmd
