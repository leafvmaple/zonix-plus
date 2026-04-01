#include "cons/cons.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"

#include "lib/memory.h"

namespace {

class ConsoleFile : public vfs::File {
public:
    int read(void* buf, size_t size, size_t offset) override {
        static_cast<void>(offset);
        if (!buf) {
            return -1;
        }

        auto* out = static_cast<char*>(buf);
        for (size_t i = 0; i < size; i++) {
            out[i] = cons::getc();
        }
        return static_cast<int>(size);
    }

    int write(const void* buf, size_t size, size_t offset) override {
        static_cast<void>(offset);
        if (!buf) {
            return -1;
        }

        const auto* in = static_cast<const char*>(buf);
        for (size_t i = 0; i < size; i++) {
            cons::putc(in[i]);
        }
        return static_cast<int>(size);
    }

    int stat(vfs::Stat* st) override {
        if (!st) {
            return -1;
        }
        st->set(vfs::NodeType::CharDevice, 0, 0);
        return 0;
    }
};

vfs::File* create_console_file() {
    return new (std::nothrow) ConsoleFile();
}

struct ConsoleDevRegistrar {
    ConsoleDevRegistrar() { vfs::register_char_dev("console", create_console_file); }
} s_console_registrar;

}  // namespace
