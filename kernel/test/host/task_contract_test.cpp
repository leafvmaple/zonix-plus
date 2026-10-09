#include "test/task_access.h"
#include "drivers/intr.h"
#include "lib/stdarg.h"
#include "lib/string.h"
#include <asm/arch.h>

extern "C" int vprintf(const char*, va_list);
extern "C" void* aligned_alloc(size_t, size_t);
extern "C" void free(void*);
extern "C" [[noreturn]] void exit(int);

class TaskFixture {
public:
    TaskFixture() {
        active_ = this;
        parent_.use_kernel_memory();
        parent_.mark_running();
        TaskManager::set_current(&parent_);
    }
    ~TaskFixture() {
        assert(live_ == 0 && TaskManager::process_count() == 0 && parent_.child_list.empty());
        TaskManager::set_current(nullptr);
        active_ = nullptr;
    }
    static TaskFixture& active() { return *active_; }
    void fail_allocation(int index) { failure_ = index; }
    void fail_open(int index) { open_failure_ = index; }
    void* allocate(size_t size) {
        if (++allocations_ == failure_)
            return nullptr;
        void* ptr = aligned_alloc(4096, (size + 4095) / 4096 * 4096);
        assert(ptr && live_ < 16);
        allocations_live_[live_++] = ptr;
        return ptr;
    }
    void deallocate(void* ptr) {
        if (!ptr)
            return;
        int slot = 0;
        while (slot < live_ && allocations_live_[slot] != ptr)
            ++slot;
        assert(slot < live_);  // Reject borrowed storage and double release.
        if (ptr == stack_) {
            assert(closed_ == 3);
            stack_freed_ = true;
        }
        allocations_live_[slot] = allocations_live_[--live_];
        free(ptr);
    }
    bool open_fails() { return ++opens_ == open_failure_; }
    void closed() { ++closed_; }
    void enter_guard() { ++guards_; }
    void leave_guard() { --guards_; }
    void root_freed(pde_t* root) {
        assert(stack_freed_ && closed_ == 3);
        ++roots_freed_;
        deallocate(root);
    }
    void check_failure(Result<int>& result, Error error, int closed) {
        assert(!result.ok() && result.error() == error);
        assert(live_ == 0 && closed_ == closed && guards_ == 0);
        assert(TaskManager::process_count() == 0 && parent_.child_list.empty());
        assert(TaskManager::task_list().empty() && roots_freed_ == 0);
    }
    void check_published(Task& task) {
        assert(task.pid == 1 && task.parent == &parent_ && task.state() == TaskState::Runnable);
        assert(TaskManager::process_count() == 1 && guards_ == 0 && live_ == 5);
        assert(task.memory() == &vmm::Manager::kernel_mm());
        assert(TaskManager::find_process(task.pid) == &task && !parent_.child_list.empty());
        stack_ = reinterpret_cast<void*>(task.kernel_stack());
    }
    void check_reaped(bool user_memory) {
        assert(live_ == 0 && closed_ == 3 && guards_ == 0 && stack_freed_);
        assert(TaskManager::process_count() == 0 && TaskManager::task_list().empty());
        assert(parent_.child_list.empty() && TaskManager::find_process(1) == nullptr);
        assert(roots_freed_ == (user_memory ? 1 : 0));
    }
    int live() const { return live_; }
    Task& parent() { return parent_; }

private:
    inline static TaskFixture* active_{};
    Task parent_;
    void* allocations_live_[16]{};
    void* stack_{};
    int live_{};
    int allocations_{};
    int failure_{};
    int opens_{};
    int open_failure_{};
    int closed_{};
    int roots_freed_{};
    int guards_{};
    bool stack_freed_{};
};

class ConsoleFixture : public vfs::File {
public:
    ~ConsoleFixture() override { TaskFixture::active().closed(); }
    Result<int> read(void*, size_t, size_t) override { return 0; }
    Result<int> write(const void*, size_t, size_t) override { return 0; }
    Error stat(vfs::Stat*) override { return Error::NotSupported; }
};

int cprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}
void panic_at(const char*, int, const char*, ...) {
    exit(77);
}
void* kmalloc(size_t size) {
    return TaskFixture::active().allocate(size);
}
void kfree(void* ptr) {
    TaskFixture::active().deallocate(ptr);
}
void* operator new(size_t size, const sys::nothrow_t&) noexcept {
    return kmalloc(size ? size : 1);
}
void operator delete(void* ptr) noexcept {
    kfree(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    kfree(ptr);
}

namespace vfs {
File::~File() = default;
Result<FileHandle> open(const char* path) {
    assert(strcmp(path, "/dev/console") == 0);
    if (TaskFixture::active().open_fails())
        return Error::Io;
    FileHandle file(new (sys::nothrow) ConsoleFixture);
    ENSURE(file, Error::NoMem);
    return file;
}
void close(File* file) {
    delete file;
}
}  // namespace vfs

namespace intr {
Guard::Guard() {
    TaskFixture::active().enter_guard();
}
Guard::~Guard() {
    TaskFixture::active().leave_guard();
}
}  // namespace intr

namespace pmm {
void free_user_pgdir(pde_t* root) {
    TaskFixture::active().root_freed(root);
}
}  // namespace pmm

void arch_fixup_fork_tf(TrapFrame*, uintptr_t) {}
void arch_set_kernel_stack(uintptr_t) {
    assert(false);
}
extern "C" void forkret() {
    assert(false);
}
extern "C" void switch_to(Context*, Context*) {
    assert(false);
}

extern "C" int main(int argc, char** argv) {
    assert(argc == 2);
    TaskFixture fixture;
    if (strcmp(argv[1], "borrowed") == 0) {
        long boot_stack[8]{};
        {
            Task idle;
            idle.use_kernel_memory();
            TaskAccess::borrow_stack(idle, reinterpret_cast<uintptr_t>(boot_stack));
            assert(idle.setup_kernel_stack() == Error::Busy);
            assert(idle.kernel_stack() == reinterpret_cast<uintptr_t>(boot_stack));
        }
        assert(fixture.live() == 0);
    } else if (strcmp(argv[1], "current") == 0) {
        fixture.parent().~Task();  // Must reject releasing the active task's resources.
    } else {
        const bool user_memory = strcmp(argv[1], "user") == 0;
        const bool open_failure =
            strcmp(argv[1], "open1") == 0 || strcmp(argv[1], "open2") == 0 || strcmp(argv[1], "open3") == 0;
        if (argv[1][0] >= '1' && argv[1][0] <= '5')
            fixture.fail_allocation(argv[1][0] - '0');
        if (open_failure)
            fixture.fail_open(argv[1][4] - '0');
        TrapFrame frame{};
        auto result = TaskManager::fork(0, 0, &frame);
        if (!result.ok()) {
            int closed = open_failure ? argv[1][4] - '1' : argv[1][0] - '2';
            if (closed < 0)
                closed = 0;
            fixture.check_failure(result, open_failure ? Error::Io : Error::NoMem, closed);
        } else {
            Task* child = TaskManager::find_process(result.value());
            assert(child);
            fixture.check_published(*child);
            const uintptr_t stack = child->kernel_stack();
            assert(child->setup_kernel_stack() == Error::Busy && child->kernel_stack() == stack);
            if (strcmp(argv[1], "linked") == 0) {
                delete child;  // Must reject freeing a task still indexed by the scheduler.
            } else {
                if (user_memory) {
                    auto memory = sys::unique_ptr<MemoryDesc>(new (sys::nothrow) MemoryDesc);
                    assert(memory);
                    memory->pgdir = static_cast<pde_t*>(kmalloc(4096));
                    assert(memory->pgdir);
                    MemoryDesc* view = memory.get();
                    child->adopt_memory(sys::move(memory));
                    assert(!memory && child->memory() == view);
                }
                child->mark_zombie(42);
                int code{};
                auto waited = TaskManager::wait(child->pid, &code);
                assert(waited.ok() && waited.value() == 1 && code == 42);
                fixture.check_reaped(user_memory);
            }
        }
    }
    cprintf("[OK] task resource contract: %s\n", argv[1]);
    return 0;
}
