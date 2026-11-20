#include "../command.h"
#include "platform.h"
#include <sys/cstdio.hpp>
#include <sys/cstdlib.hpp>
#include <sys/new.hpp>

namespace {

// The heap lives above the program image and grows by moving the program
// break, so a program takes only the memory it uses. Blocks are laid out back
// to back from the first break to the current one; freed neighbours merge, and
// a large free block at the end is given back to the kernel.
class Heap {
public:
    static void* allocate(sys::size_t size) {
        if (size == 0)
            size = 1;
        if (size > MAX_REQUEST)
            return nullptr;
        size = round(size);
        if (!base_ && !start())
            return nullptr;
        for (Block* block = first(); block; block = next(block)) {
            if (block->free && block->size >= size)
                return take(block, size);
        }
        Block* block = extend(size);
        return block ? take(block, size) : nullptr;
    }

    static void release(void* pointer) {
        if (!pointer)
            return;
        (static_cast<Block*>(pointer) - 1)->free = true;
        Block* last = nullptr;
        for (Block* block = first(); block; block = next(block)) {
            if (block->free)
                merge(block);
            last = block;
        }
        if (last && last->free)
            trim(last);
    }

    static void* resize(void* pointer, sys::size_t size) {
        if (!pointer)
            return allocate(size);
        if (size == 0) {
            release(pointer);
            return nullptr;
        }
        auto* block = static_cast<Block*>(pointer) - 1;
        if (size <= block->size)
            return pointer;
        void* replacement = allocate(size);
        if (replacement) {
            memcpy(replacement, pointer, block->size);
            release(pointer);
        }
        return replacement;
    }

private:
    static constexpr sys::size_t ALIGN = 16;
    // The break moves in steps of at least this much, so small allocations
    // do not each cost a system call.
    static constexpr sys::size_t GROWTH = 256 * 1024;
    // A free block at the end larger than this is returned to the kernel,
    // keeping GROWTH bytes for the next allocations.
    static constexpr sys::size_t TRIM = 1024 * 1024;
    static constexpr sys::size_t MAX_REQUEST = sys::size_t{1} << 40;

    struct alignas(ALIGN) Block {
        sys::size_t size;
        bool free;
    };

    inline static unsigned char* base_{};
    inline static unsigned char* end_{};

    static sys::size_t round(sys::size_t size) { return (size + ALIGN - 1) & ~(ALIGN - 1); }

    // The heap starts at the first break, aligned for blocks.
    static bool start() {
        long brk = sys_brk(0);
        if (brk <= 0)
            return false;
        auto aligned = (static_cast<unsigned long>(brk) + ALIGN - 1) & ~(ALIGN - 1);
        if (sys_brk(static_cast<long>(aligned)) != static_cast<long>(aligned))
            return false;
        base_ = end_ = reinterpret_cast<unsigned char*>(aligned);
        return true;
    }

    // Moves the end of the heap, or returns false and leaves it.
    static bool move_end(unsigned char* end) {
        auto target = reinterpret_cast<unsigned long>(end);
        if (sys_brk(static_cast<long>(target)) != static_cast<long>(target))
            return false;
        end_ = end;
        return true;
    }

    static Block* first() { return base_ != end_ ? reinterpret_cast<Block*>(base_) : nullptr; }

    static Block* next(Block* block) {
        auto* address = reinterpret_cast<unsigned char*>(block + 1) + block->size;
        return address == end_ ? nullptr : reinterpret_cast<Block*>(address);
    }

    static Block* last() {
        Block* found = nullptr;
        for (Block* block = first(); block; block = next(block))
            found = block;
        return found;
    }

    // Grows the heap until its last block is free and holds `size` bytes.
    static Block* extend(sys::size_t size) {
        Block* tail = last();
        bool reuse = tail && tail->free;
        sys::size_t have = reuse ? tail->size : 0;
        sys::size_t need = size - have + (reuse ? 0 : sizeof(Block));
        sys::size_t step = need > GROWTH ? round(need) : GROWTH;
        unsigned char* old_end = end_;
        if (!move_end(end_ + step))
            return nullptr;
        if (reuse) {
            tail->size += step;
            return tail;
        }
        return new (old_end) Block{step - sizeof(Block), true};
    }

    static void* take(Block* block, sys::size_t size) {
        split(block, size);
        block->free = false;
        return block + 1;
    }

    static void split(Block* block, sys::size_t size) {
        if (block->size - size < sizeof(Block) + ALIGN)
            return;
        auto* address = reinterpret_cast<unsigned char*>(block + 1) + size;
        new (address) Block{block->size - size - sizeof(Block), true};
        block->size = size;
    }

    static void merge(Block* block) {
        while (auto* following = next(block)) {
            if (!following->free)
                break;
            block->size += sizeof(Block) + following->size;
        }
    }

    static void trim(Block* tail) {
        if (tail->size <= TRIM)
            return;
        sys::size_t release = round(tail->size - GROWTH);
        if (move_end(end_ - release))
            tail->size -= release;
    }
};

class ExitHandlers {
public:
    static int add(void (*handler)()) {
        if (count_ == CAPACITY)
            return -1;
        handlers_[count_++] = handler;
        return 0;
    }
    static void run() {
        while (count_)
            handlers_[--count_]();
    }

private:
    static constexpr unsigned int CAPACITY = 32;
    inline static void (*handlers_[CAPACITY])(){};
    inline static unsigned int count_{};
};

int digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 36;
}

}  // namespace

namespace zonix_gui {
void finish() {
    ExitHandlers::run();
}
}  // namespace zonix_gui

extern "C" void* malloc(sys::size_t size) {
    return Heap::allocate(size);
}

extern "C" void free(void* ptr) {
    Heap::release(ptr);
}

extern "C" void* realloc(void* ptr, sys::size_t size) {
    return Heap::resize(ptr, size);
}

extern "C" [[noreturn]] void abort() {
    command_text(STDERR_FD, "GUI runtime: aborted\n");
    sys_exit(1);
    __builtin_unreachable();
}

extern "C" [[noreturn]] void __cxa_pure_virtual() {
    abort();
}

extern "C" int atexit(void (*handler)()) {
    return ExitHandlers::add(handler);
}

void* operator new(sys::size_t size) {
    if (void* memory = Heap::allocate(size))
        return memory;
    command_text(STDERR_FD, "GUI runtime: heap exhausted\n");
    abort();
}

void* operator new(sys::size_t size, const sys::nothrow_t&) noexcept {
    return Heap::allocate(size);
}

void operator delete(void* ptr) noexcept {
    Heap::release(ptr);
}

void operator delete(void* ptr, sys::size_t) noexcept {
    Heap::release(ptr);
}

void operator delete(void* ptr, const sys::nothrow_t&) noexcept {
    Heap::release(ptr);
}

extern "C" long strtol(const char* text, char** end, int base) {
    const char* start = text;
    while (*text == ' ' || (*text >= '\t' && *text <= '\r'))
        ++text;
    bool negative = *text == '-';
    if (*text == '+' || *text == '-')
        ++text;
    if ((base == 0 || base == 16) && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') && digit(text[2]) < 16) {
        text += 2;
        base = 16;
    }
    if (base == 0)
        base = *text == '0' ? 8 : 10;
    const char* digits = text;
    unsigned long value = 0;
    unsigned long limit = static_cast<unsigned long>(__LONG_MAX__) + (negative ? 1 : 0);
    if (base >= 2 && base <= 36) {
        for (int d = digit(*text); d < base; d = digit(*++text))
            value = value > (limit - d) / base ? limit : value * base + d;
    }
    if (end)
        *end = const_cast<char*>(text == digits ? start : text);
    if (!negative)
        return static_cast<long>(value);
    return value > static_cast<unsigned long>(__LONG_MAX__) ? -__LONG_MAX__ - 1 : -static_cast<long>(value);
}

// mini-cocos diagnostics use literal strings and %s on stderr.
struct zstl_file {
    int fd;
};
static zstl_file diagnostic_stream{STDERR_FD};
FILE* stderr = &diagnostic_stream;

extern "C" int fprintf(FILE* stream, const char* format, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, format);
    int total = 0;
    while (*format) {
        const char* text = format;
        long count = 0;
        while (format[count] && format[count] != '%')
            ++count;
        if (count != 0) {
            format += count;
        } else if (format[1] == 's') {
            text = __builtin_va_arg(args, const char*);
            if (!text)
                text = "(null)";
            count = 0;
            while (text[count])
                ++count;
            format += 2;
        } else if (format[1] == '%') {
            count = 1;
            format += 2;
        } else {
            total = -1;
            break;
        }
        if (command_write_all(stream->fd, text, count) < 0) {
            total = -1;
            break;
        }
        total += static_cast<int>(count);
    }
    __builtin_va_end(args);
    return total;
}
