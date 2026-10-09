#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include "debug/assert.h"
#include "lib/memory.h"
#include "lib/stdarg.h"
#include "mm/pmm.h"

extern "C" int vprintf(const char*, va_list);
extern "C" [[noreturn]] void exit(int);

class InterruptFixture {
public:
    static int enter() { return depth_++; }
    static void leave() { --depth_; }
    static void on_scroll() {
        ++scrolls_;
        // Simulate a timer/context switch precisely while putc is scrolling
        // with a transient out-of-range cursor. Disabled interrupts defer it.
        if (depth_ == 0 && !injected_) {
            injected_ = true;
            fbcons::putc('X');
        }
    }
    static bool protected_scrolls() { return scrolls_ > 0 && !injected_ && depth_ == 0; }

private:
    inline static int depth_{};
    inline static int scrolls_{};
    inline static bool injected_{};
};

intr::Guard::Guard() : flag_(InterruptFixture::enter()) {}
intr::Guard::~Guard() {
    InterruptFixture::leave();
}

void pmm::free_user_pgdir(pde_t*) {
    assert(false);
}

extern "C" void* memmove(void* destination, const void* source, size_t size) {
    InterruptFixture::on_scroll();
    auto* dst = static_cast<uint8_t*>(destination);
    auto* src = static_cast<const uint8_t*>(source);
    if (dst <= src) {
        for (size_t i = 0; i < size; ++i) {
            dst[i] = src[i];
        }
    } else {
        for (size_t i = size; i > 0; --i) {
            dst[i - 1] = src[i - 1];
        }
    }
    return destination;
}

void panic_at(const char*, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    exit(line);
}

extern "C" int main() {
    constexpr size_t width = 128;
    constexpr size_t height = 128;
    constexpr size_t extra_rows = 32;
    constexpr uint32_t canary = 0xDEADBEEF;
    uint32_t pixels[width * (height + extra_rows)];
    for (auto& pixel : pixels) {
        pixel = canary;
    }
    fbcons::init(reinterpret_cast<uintptr_t>(pixels), width, height, width * 4, 32);
    assert(fbcons::is_active());
    for (int i = 0; i < 1000; ++i) {
        fbcons::putc('A');
        fbcons::putc('\n');
        fbcons::tick();
    }
    assert(InterruptFixture::protected_scrolls());
    for (size_t i = width * height; i < width * (height + extra_rows); ++i) {
        assert(pixels[i] == canary);
    }
    return 0;
}
