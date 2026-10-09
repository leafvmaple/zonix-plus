#include "lib/kernel_buffer.h"
#include "lib/stdarg.h"
#include "fs/vfs.h"

extern "C" int vprintf(const char*, va_list);
extern "C" void* aligned_alloc(size_t, size_t);
extern "C" void free(void*);
extern "C" [[noreturn]] void exit(int);

namespace ownership_test {
void test();
}

class AllocatorFixture {
public:
    static void fail_next() { fail_ = true; }
    static void* allocate(size_t size) {
        if (fail_) {
            fail_ = false;
            return nullptr;
        }
        void* ptr = aligned_alloc(4096, (size + 4095) / 4096 * 4096);
        assert(ptr);
        ++live_;
        return ptr;
    }
    static void deallocate(void* ptr) {
        if (ptr) {
            --live_;
            free(ptr);
        }
    }
    static int live() { return live_; }

private:
    inline static bool fail_{};
    inline static int live_{};
};

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
void* kmalloc(size_t size) {
    return AllocatorFixture::allocate(size);
}
void kfree(void* ptr) {
    AllocatorFixture::deallocate(ptr);
}
void* operator new(size_t size, const sys::nothrow_t&) noexcept {
    return kmalloc(size ? size : 1);
}
void* operator new[](size_t size, const sys::nothrow_t&) noexcept {
    return operator new(size, sys::nothrow);
}
void* operator new(size_t size) {
    void* ptr = operator new(size, sys::nothrow);
    assert(ptr);
    return ptr;
}
void* operator new[](size_t size) {
    return operator new(size);
}
void operator delete(void* ptr) noexcept {
    kfree(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    kfree(ptr);
}
void operator delete[](void* ptr) noexcept {
    kfree(ptr);
}
void operator delete[](void* ptr, size_t) noexcept {
    kfree(ptr);
}

namespace vfs {
File::~File() = default;
void close(File* file) {
    delete file;
}
}  // namespace vfs

extern "C" int main(int argc, char**) {
    if (argc == 1) {
        ownership_test::test();
    } else {
        AllocatorFixture::fail_next();
        auto buffer = KernelBuffer::alloc(32);
        assert(!buffer.ok() && buffer.error() == Error::NoMem);
        cprintf("[OK] failed allocation creates no owner\n");
    }
    assert(AllocatorFixture::live() == 0);
    return 0;
}
