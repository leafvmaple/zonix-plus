#include "cons/cons.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"

#include "lib/memory.h"

namespace {

class ConsoleFile : public vfs::File {
public:
    Result<int> read(void* buf, size_t size, size_t offset) override {
        static_cast<void>(offset);
        ENSURE(buf, Error::Invalid);

        auto* out = static_cast<char*>(buf);
        for (size_t i = 0; i < size; i++) {
            auto next = cons::read_input();
            if (!next.ok())
                return i != 0 ? Result<int>(static_cast<int>(i)) : Result<int>(next.error());
            out[i] = next.value();
        }
        return static_cast<int>(size);
    }

    Result<int> write(const void* buf, size_t size, size_t offset) override {
        static_cast<void>(offset);
        ENSURE(buf, Error::Invalid);

        const auto* in = static_cast<const char*>(buf);
        for (size_t i = 0; i < size; i++) {
            cons::write_output(in[i]);
        }
        return static_cast<int>(size);
    }

    Error control(unsigned int request, void* value) override {
        if (request == TERMINAL_GET_SIZE) {
            *static_cast<TerminalSize*>(value) = cons::screen_size();
            return Error::None;
        }
        if (request == TERMINAL_SET_RAW)
            return cons::set_raw(*static_cast<unsigned int*>(value) != 0);
        return Error::NotSupported;
    }

    Error stat(vfs::Stat* st) override {
        ENSURE(st, Error::Invalid);
        st->set(vfs::NodeType::CharDevice, 0, 0);
        return Error::None;
    }
};

Result<vfs::FileHandle> create_console_file() {
    vfs::FileHandle file(new (sys::nothrow) ConsoleFile());
    ENSURE(file, Error::NoMem);
    return file;
}

struct ConsoleDevRegistrar {
    ConsoleDevRegistrar() { assert(vfs::register_char_dev("console", create_console_file) == Error::None); }
} console_registrar;

}  // namespace
