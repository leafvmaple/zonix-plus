#include "lib/stdio.h"
#include "cons.h"
#include "lib/stdarg.h"
#include "lib/string.h"
#include <base/types.h>

namespace {

enum class FmtState : uint8_t { None, Spec };

constexpr int NUM_BUF_SIZE = 21;

struct ConsoleSink {
    size_t pos{};
    void put(char c) {
        cons::putc(c);
        pos++;
    }
};

struct BufferSink {
    char* buf{};
    size_t pos{};
    size_t size{};
    void put(char c) {
        if (pos < size - 1)
            buf[pos] = c;
        pos++;
    }
};

template<typename Sink>
void put_pad(Sink& sink, char c, int n) {
    while (n-- > 0)
        sink.put(c);
}

template<typename Sink>
void put_int(Sink& sink, uint64_t num, uint32_t base, bool negative, int width, char pad_chr, bool left_align) {
    char buf[NUM_BUF_SIZE];
    int len = 0;

    if (num == 0) {
        buf[len++] = '0';
    } else {
        while (num > 0) {
            uint32_t d = num % base;
            buf[len++] = d < 10 ? '0' + d : 'A' + d - 10;
            num /= base;
        }
    }

    int total = len + (negative ? 1 : 0);
    int pad = (width > total) ? width - total : 0;

    if (!left_align && pad_chr == ' ')
        put_pad(sink, ' ', pad);

    if (negative)
        sink.put('-');

    if (!left_align && pad_chr == '0')
        put_pad(sink, '0', pad);

    while (len > 0)
        sink.put(buf[--len]);

    if (left_align)
        put_pad(sink, ' ', pad);
}

template<typename Sink>
void put_str(Sink& sink, const char* s, int width, bool left_align) {
    int len = strlen(s);
    int pad = (width > len) ? width - len : 0;

    if (!left_align)
        put_pad(sink, ' ', pad);

    while (*s)
        sink.put(*s++);

    if (left_align)
        put_pad(sink, ' ', pad);
}

template<typename Sink>
int vformat(Sink& sink, const char* fmt, va_list args) {
    auto state = FmtState::None;
    int width{};
    bool long_flag{};
    bool left_align{};
    char pad_chr{};

    char c{};
    while ((c = *fmt++)) {
        switch (state) {
            case FmtState::None:
                if (c == '%') {
                    state = FmtState::Spec;
                    width = 0;
                    long_flag = false;
                    left_align = false;
                    pad_chr = ' ';
                } else {
                    sink.put(c);
                }
                break;
            case FmtState::Spec:
                switch (c) {
                    case '-': left_align = true; break;
                    case '0' ... '9':
                        if (c == '0' && width == 0)
                            pad_chr = '0';
                        else
                            width = width * 10 + (c - '0');
                        break;
                    case 'l': long_flag = true; break;
                    case 'x':
                    case 'p': {
                        uint64_t num = long_flag ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                        put_int(sink, num, 16, false, width, pad_chr, left_align);
                        state = FmtState::None;
                        break;
                    }
                    case 'u': {
                        uint64_t num = long_flag ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                        put_int(sink, num, 10, false, width, pad_chr, left_align);
                        state = FmtState::None;
                        break;
                    }
                    case 'd': {
                        int64_t snum = long_flag ? va_arg(args, int64_t) : va_arg(args, int32_t);
                        bool neg = snum < 0;
                        put_int(sink, neg ? -snum : snum, 10, neg, width, pad_chr, left_align);
                        state = FmtState::None;
                        break;
                    }
                    case 's': {
                        const char* s = va_arg(args, const char*);
                        put_str(sink, s, width, left_align);
                        state = FmtState::None;
                        break;
                    }
                    case 'c':
                        sink.put(va_arg(args, int));
                        state = FmtState::None;
                        break;
                    case '%':
                        sink.put('%');
                        state = FmtState::None;
                        break;
                    default: state = FmtState::None; break;
                }
                break;
        }
    }

    return sink.pos;
}

}  // namespace

int cprintf(const char* fmt, ...) {
    ConsoleSink sink{0};
    va_list args;
    va_start(args, fmt);
    int n = vformat(sink, fmt, args);
    va_end(args);
    return n;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    if (size == 0)
        return 0;

    BufferSink sink{buf, 0, size};
    va_list args;
    va_start(args, fmt);
    int n = vformat(sink, fmt, args);
    va_end(args);
    buf[sink.pos < size ? sink.pos : size - 1] = '\0';
    return n;
}
