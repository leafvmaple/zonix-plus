#pragma once

#include "sched/sched.h"

// Private resource access stays in tests; production callers move owners into Task.
struct TaskAccess {
    static uintptr_t kernel_stack(const Task* task) { return task->kernel_stack(); }
    static void borrow_stack(Task& task, uintptr_t stack) {
        assert(task.kernel_stack_.empty());
        task.boot_stack_ = stack;
    }
    static MemoryDesc* borrow_memory(Task& task, MemoryDesc* memory) {
        assert(!task.owned_memory_);
        return sys::exchange(task.borrowed_memory_, memory);
    }
    static sys::unique_ptr<MemoryDesc> take_memory(Task& task) { return sys::move(task.owned_memory_); }
};

class ScopedTaskMemory {
public:
    ScopedTaskMemory(Task& task, MemoryDesc& memory)
        : task_(task), previous_(TaskAccess::borrow_memory(task, &memory)) {}
    ~ScopedTaskMemory() { TaskAccess::borrow_memory(task_, previous_); }
    ScopedTaskMemory(const ScopedTaskMemory&) = delete;
    ScopedTaskMemory& operator=(const ScopedTaskMemory&) = delete;

private:
    Task& task_;
    MemoryDesc* previous_;
};
