#include "../command.h"
#include <sys/array.hpp>
#include <sys/new.hpp>

namespace {

// This process allocates its texture and command storage during setup.
// The framebuffer is borrowed, and every later frame reuses that storage.
class SetupArena {
public:
    static void* allocate(sys::size_t size) {
        auto first = (used_ + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
        if (size == 0)
            size = 1;
        if (first > bytes_.size() || size > bytes_.size() - first) {
            command_text(STDERR_FD, "minicoco: setup arena exhausted\n");
            sys_exit(1);
            __builtin_unreachable();
        }
        used_ = first + size;
        return bytes_.data() + first;
    }

private:
    static constexpr sys::size_t ALIGNMENT = 16;
    alignas(ALIGNMENT) inline static sys::array<unsigned char, 4096> bytes_{};
    inline static sys::size_t used_{};
};

}  // namespace

void* operator new(sys::size_t size) {
    return SetupArena::allocate(size);
}

// The application's arena is reclaimed with its address space.
void operator delete(void*) noexcept {}
void operator delete(void*, sys::size_t) noexcept {}
