#include "lib/result.h"
#include "lib/stdarg.h"

extern "C" int vprintf(const char* format, va_list args);
extern "C" [[noreturn]] void exit(int code);

namespace result_test {
void test();
}

int cprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int written = vprintf(format, args);
    va_end(args);
    return written;
}

void panic_at(const char*, int, const char*, ...) {
    exit(77);
}

extern "C" int main(int argc, char** argv) {
    if (argc == 1) {
        result_test::test();
        return 0;
    }
    switch (argv[1][0]) {
        case 'n': {
            Result<int> result = Error::None;
            return result.ok();
        }
        case 'v': {
            Result<int> result = Error::Io;
            return result.value();
        }
        case 'e': {
            Result<int> result = 42;
            return static_cast<int>(result.release_error());
        }
        case 't': {
            Result<int> result = 42;
            (void)result.release_value();
            return result.value();
        }
        case 'q': {
            Result<int> result = Error::Io;
            (void)result.release_error();
            return static_cast<int>(result.error());
        }
        case 'f': {
            Result<int> result = 42;
            (void)result.release_value();
            return result.value_or(99);
        }
        case 'w': {
            Result<void> result = Error::Io;
            result.release_value();
            return 0;
        }
        case 's': {
            Result<void> result;
            return static_cast<int>(result.release_error());
        }
        case 'x': {
            Result<void> result;
            result.release_value();
            result.release_value();
            return 0;
        }
        case 'y': {
            Result<void> result = Error::Io;
            (void)result.release_error();
            return static_cast<int>(result.error());
        }
    }
    return 1;
}
