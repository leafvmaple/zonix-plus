#include "syscall.h"
#include "exec/exec.h"
#include "sched/sched.h"
#include "lib/kernel_buffer.h"
#include "fs/vfs.h"
#include <abi/process.h>
#include "drivers/intr.h"

static_assert(sizeof(ProcessInfo) == 48);
static_assert(exec::MAX_ARGS == PROCESS_MAX_ARGS && exec::MAX_ARG_BYTES == PROCESS_MAX_ARG_BYTES);
static_assert(exec::MAX_PATH == PROCESS_MAX_PATH && sizeof(FileStat) == 12 && sizeof(DirectoryEntry) == 44);
static_assert(static_cast<int>(Error::Invalid) == SYS_ERROR_INVALID && static_cast<int>(Error::Full) == SYS_ERROR_FULL);

namespace {

Result<int> spawn_user(Task* task, const char* path, long argc, uintptr_t argv, bool foreground) {
    ENSURE(task && argc >= 0, Error::Invalid);
    ENSURE(static_cast<size_t>(argc) <= exec::MAX_ARGS, Error::Full);
    char copied_path[exec::MAX_PATH]{};
    TRY(syscall::copy_path(task, path, copied_path));
    auto args = sys::unique_ptr<exec::Arguments>(new (sys::nothrow) exec::Arguments());
    ENSURE(args, Error::NoMem);
    if (argc != 0 || argv != 0) {
        ENSURE(vmm::user_range_valid(task->memory(), argv, (argc + 1) * sizeof(uintptr_t), false), Error::Invalid);
        auto scratch = TRY(KernelBuffer::alloc(exec::MAX_ARG_BYTES));
        for (long i = 0; i <= argc; ++i) {
            uintptr_t text{};
            TRY(vmm::copy_from_user(task->memory(), &text, argv + i * sizeof(text), sizeof(text)));
            if (i == argc) {
                ENSURE(text == 0, Error::Invalid);
                break;
            }
            TRY(syscall::copy_string(task, text, reinterpret_cast<char*>(scratch.data()), scratch.size()));
            TRY(args->add(reinterpret_cast<const char*>(scratch.data())));
        }
    }
    return exec::spawn(copied_path, *args, foreground);
}

class EntryAt : public vfs::DirVisitor {
public:
    explicit EntryAt(size_t index) : index_(index) {}
    int visit(const vfs::DirEntry& entry) override {
        if (position_++ != index_)
            return 0;
        strncpy(value.name, entry.name, sizeof(value.name));
        value.type = static_cast<unsigned int>(entry.type);
        value.size = entry.size;
        value.attrs = entry.attrs;
        found = true;
        return 1;
    }
    DirectoryEntry value{};
    bool found{};

private:
    size_t index_;
    size_t position_{};
};

}  // namespace

Error syscall::copy_string(Task* task, uintptr_t user, char* out, size_t capacity) {
    ENSURE(task && user && out && capacity, Error::Invalid);
    for (size_t i = 0; i < capacity; ++i) {
        ENSURE(user < USER_SPACE_TOP && i < USER_SPACE_TOP - user, Error::Invalid);
        TRY(vmm::copy_from_user(task->memory(), out + i, user + i, 1));
        if (out[i] == '\0')
            return Error::None;
    }
    return Error::Invalid;
}

Error syscall::copy_path(Task* task, const char* user, char (&path)[exec::MAX_PATH]) {
    char source[exec::MAX_PATH]{};
    TRY(copy_string(task, reinterpret_cast<uintptr_t>(user), source, sizeof(source)));
    ENSURE(source[0], Error::Invalid);
    size_t used = 1;
    path[0] = '/';
    if (source[0] != '/') {
        used = strlen(task->cwd());
        memcpy(path, task->cwd(), used);
    }
    const char* part = source;
    while (*part) {
        while (*part == '/')
            ++part;
        const char* end = part;
        while (*end && *end != '/')
            ++end;
        size_t length = end - part;
        if (length == 2 && part[0] == '.' && part[1] == '.') {
            while (used > 1 && path[used - 1] != '/')
                --used;
            if (used > 1)
                --used;
        } else if (length && !(length == 1 && part[0] == '.')) {
            size_t separator = used > 1 ? 1 : 0;
            ENSURE(used + separator + length < exec::MAX_PATH, Error::Full);
            if (separator)
                path[used++] = '/';
            memcpy(path + used, part, length);
            used += length;
        }
        // A slash requires a real directory, even before a following "..".
        path[used] = '\0';
        if (*end == '/' && length != 0) {
            vfs::Stat st{};
            TRY(vfs::stat(path, &st));
            ENSURE(st.type == vfs::NodeType::Directory, Error::Invalid);
        }
        part = end;
    }
    path[used] = '\0';
    return Error::None;
}

long syscall::chdir(Task* task, const char* path) {
    char copied[exec::MAX_PATH]{};
    Error error = copy_path(task, path, copied);
    if (error != Error::None)
        return static_cast<long>(error);
    vfs::Stat st{};
    error = vfs::stat(copied, &st);
    if (error != Error::None)
        return static_cast<long>(error);
    if (st.type != vfs::NodeType::Directory)
        return static_cast<long>(Error::Invalid);
    task->set_cwd(copied);
    return 0;
}

long syscall::getcwd(Task* task, uintptr_t output, size_t capacity) {
    if (!task)
        return static_cast<long>(Error::Invalid);
    size_t size = strlen(task->cwd()) + 1;
    if (capacity < size)
        return static_cast<long>(Error::Full);
    return static_cast<long>(vmm::copy_to_user(task->memory(), output, task->cwd(), size));
}

long syscall::brk(Task* task, uintptr_t end) {
    if (!task)
        return static_cast<long>(Error::Invalid);
    auto moved = vmm::set_break(task->memory(), end);
    return moved.ok() ? static_cast<long>(moved.value()) : static_cast<long>(moved.error());
}

long syscall::spawn(Task* task, const char* path, long argc, uintptr_t argv, bool foreground) {
    auto result = spawn_user(task, path, argc, argv, foreground);
    return result.ok() ? result.value() : static_cast<long>(result.error());
}

long syscall::wait(Task* task, long pid, uintptr_t status) {
    if (!task || pid < 0 || pid > 0x7FFFFFFF)
        return static_cast<long>(Error::Invalid);
    if (status && !vmm::user_range_valid(task->memory(), status, sizeof(int), true))
        return static_cast<long>(Error::Invalid);
    int code{};
    auto ready = TaskManager::wait_ready(static_cast<int>(pid), &code);
    if (!ready.ok())
        return static_cast<long>(ready.error());
    if (status) {
        Error error = vmm::copy_to_user(task->memory(), status, &code, sizeof(code));
        if (error != Error::None)
            return static_cast<long>(error);
    }
    auto reaped = sched::wait(ready.value(), nullptr);
    return reaped.ok() ? reaped.value() : static_cast<long>(reaped.error());
}

static_assert(PROCESS_PRIORITY_DEFAULT == sched_prio::DEFAULT &&
              PROCESS_PRIORITY_LOWEST == sched_prio::LOWEST_PRIORITY &&
              PROCESS_PRIORITY_HIGHEST > sched_prio::HIGHEST_PRIORITY);

// A process sets its own priority (pid 0) or that of a child it started, as a
// compositor keeps itself ahead of its applications.
long syscall::set_priority(Task* task, long pid, long priority) {
    if (!task || pid < 0 || pid > 0x7FFFFFFF || priority < PROCESS_PRIORITY_HIGHEST ||
        priority > PROCESS_PRIORITY_LOWEST)
        return static_cast<long>(Error::Invalid);
    intr::Guard guard;
    Task* target = pid == 0 ? task : TaskManager::find_process(static_cast<int>(pid));
    // A child that has exited but is not reaped yet is still the caller's.
    if (!target || (target != task && target->parent != task))
        return static_cast<long>(Error::NotFound);
    const int previous = target->priority;
    target->priority = static_cast<int>(priority);
    // Lowering itself, the caller gives way to anything that now outranks it.
    if (target == task && priority > previous)
        task->need_resched = 1;
    return 0;
}

long syscall::process_next(Task* task, long after_pid, uintptr_t output) {
    if (!task || after_pid < -1 || after_pid > 0x7FFFFFFF ||
        !vmm::user_range_valid(task->memory(), output, sizeof(ProcessInfo), true))
        return static_cast<long>(Error::Invalid);
    ProcessInfo info{};
    auto next = TaskManager::next_process(static_cast<int>(after_pid), info);
    if (!next.ok())
        return static_cast<long>(next.error());
    if (!next.value())
        return 0;
    Error error = vmm::copy_to_user(task->memory(), output, &info, sizeof(info));
    return error == Error::None ? 1 : static_cast<long>(error);
}

long syscall::stat(Task* task, const char* path, uintptr_t output) {
    if (!task || !vmm::user_range_valid(task->memory(), output, sizeof(FileStat), true))
        return static_cast<long>(Error::Invalid);
    char copied[exec::MAX_PATH]{};
    Error error = copy_path(task, path, copied);
    if (error != Error::None)
        return static_cast<long>(error);
    vfs::Stat st{};
    error = vfs::stat(copied, &st);
    if (error != Error::None)
        return static_cast<long>(error);
    FileStat result{static_cast<unsigned int>(st.type), st.size, st.attrs};
    return static_cast<long>(vmm::copy_to_user(task->memory(), output, &result, sizeof(result)));
}

long syscall::readdir(Task* task, const char* path, long index, uintptr_t output) {
    if (!task || index < 0 || !vmm::user_range_valid(task->memory(), output, sizeof(DirectoryEntry), true))
        return static_cast<long>(Error::Invalid);
    char copied[exec::MAX_PATH]{};
    Error error = copy_path(task, path, copied);
    if (error != Error::None)
        return static_cast<long>(error);
    EntryAt visitor(static_cast<size_t>(index));
    auto result = vfs::readdir(copied, visitor);
    if (!result.ok())
        return static_cast<long>(result.error());
    if (!visitor.found)
        return 0;
    error = vmm::copy_to_user(task->memory(), output, &visitor.value, sizeof(visitor.value));
    return error == Error::None ? 1 : static_cast<long>(error);
}
