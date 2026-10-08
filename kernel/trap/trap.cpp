#include "trap/trap.h"

#include "lib/unistd.h"
#include "lib/stdio.h"

#include <asm/page.h>

#include "drivers/fbcons.h"
#include "fs/vfs.h"
#include "lib/result.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "lib/memory.h"
#include "debug/assert.h"

namespace timer {
extern volatile int64_t ticks;
}

namespace {

constexpr size_t SYSCALL_PATH_MAX = 128;

int copy_user_cstr(TaskStruct* cur, const char* user, char* out, size_t out_size) {
    if (!cur || !user || !out || out_size == 0) {
        return -1;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(user);
    if (base >= USER_SPACE_TOP) {
        return -1;
    }

    for (size_t i = 0; i < out_size; i++) {
        uintptr_t addr = base + i;
        if (addr >= USER_SPACE_TOP) {
            return -1;
        }

        char ch;
        if (vmm::copy_from_user(cur->memory, &ch, addr, 1) != Error::None) {
            return -1;
        }
        out[i] = ch;
        if (ch == '\0') {
            return 0;
        }
    }

    out[out_size - 1] = '\0';
    return -1;
}

long sys_open(TaskStruct* cur, const char* user_path, int flags, int mode) {
    static_cast<void>(flags);
    static_cast<void>(mode);

    if (!cur) {
        return -1;
    }

    char path[SYSCALL_PATH_MAX]{};
    if (copy_user_cstr(cur, user_path, path, sizeof(path)) != 0) {
        return -1;
    }

    vfs::File* file = nullptr;
    if (vfs::open(path, &file) != Error::None || !file) {
        return -1;
    }

    auto fd_r = cur->files().alloc(file);
    if (!fd_r.ok()) {
        vfs::close(file);
        return -1;
    }

    return fd_r.value();
}

enum class FileIo { Read, Write };

long sys_file_io(TaskStruct* cur, int fd, uintptr_t user_buf, size_t count, FileIo operation) {
    if (count == 0) {
        return 0;
    }
    const bool to_user = operation == FileIo::Read;
    if (!cur || !vmm::user_range_valid(cur->memory, user_buf, count, to_user)) {
        return -1;
    }
    fd::Entry* entry = cur->files().get(fd);
    if (!entry) {
        return -1;
    }

    auto* buf = static_cast<uint8_t*>(kmalloc(PG_SIZE));
    if (!buf) {
        return -1;
    }
    size_t done = 0;
    bool failed = false;
    while (done < count) {
        size_t chunk = count - done;
        if (chunk > PG_SIZE) {
            chunk = PG_SIZE;
        }
        if (!to_user && vmm::copy_from_user(cur->memory, buf, user_buf + done, chunk) != Error::None) {
            failed = true;
            break;
        }
        auto bytes_r = to_user ? vfs::read(entry->file, buf, chunk, entry->offset)
                               : vfs::write(entry->file, buf, chunk, entry->offset);
        if (!bytes_r.ok() || bytes_r.value() < 0 || static_cast<size_t>(bytes_r.value()) > chunk) {
            failed = true;
            break;
        }
        size_t bytes = static_cast<size_t>(bytes_r.value());
        if (to_user && vmm::copy_to_user(cur->memory, user_buf + done, buf, bytes) != Error::None) {
            failed = true;
            break;
        }
        entry->offset += bytes;
        done += bytes;
        if (bytes < chunk) {
            break;
        }
    }
    kfree(buf);
    return failed && done == 0 ? -1 : static_cast<long>(done);
}

long sys_read(TaskStruct* cur, int fd, void* user_buf, size_t count) {
    if (!cur) {
        return -1;
    }
    return sys_file_io(cur, fd, reinterpret_cast<uintptr_t>(user_buf), count, FileIo::Read);
}

long sys_close(TaskStruct* cur, int fd) {
    if (!cur) {
        return -1;
    }
    return cur->files().close(fd) == Error::None ? 0 : -1;
}

long sys_write(TaskStruct* cur, int fd, const char* user_buf, size_t count) {
    return sys_file_io(cur, fd, reinterpret_cast<uintptr_t>(user_buf), count, FileIo::Write);
}

}  // namespace

namespace trap {

void handle_timer_tick() {
    timer::ticks++;
    sched::tick();
    fbcons::tick();
}

int handle_page_fault(TrapFrame* tf, uint32_t err, uintptr_t fault_addr) {
    if (!tf) {
        return -1;
    }

    tf->print();
    tf->print_pgfault();

    TaskStruct* current = sched::current();
    if (!current || !current->memory) {
        return -1;
    }

    return vmm::pg_fault(current->memory, err, fault_addr);
}

bool handle_syscall(TrapFrame* tf) {
    if (!tf) {
        return false;
    }

    int nr = static_cast<int>(tf->syscall_nr());
    TaskStruct* cur = sched::current();

    switch (nr) {
        case NR_EXIT:
            cprintf("[PID %d] exited with code %ld\n", cur ? cur->pid : -1, tf->syscall_arg(0));
            sched::exit(static_cast<int>(tf->syscall_arg(0)));
            return true;
        case NR_OPEN: {
            const auto* path = reinterpret_cast<const char*>(tf->syscall_arg(0));
            int flags = static_cast<int>(tf->syscall_arg(1));
            int mode = static_cast<int>(tf->syscall_arg(2));
            long rc = sys_open(cur, path, flags, mode);
            tf->set_return(static_cast<uint64_t>(rc));
            return true;
        }
        case NR_READ: {
            int fd = static_cast<int>(tf->syscall_arg(0));
            auto* buf = reinterpret_cast<void*>(tf->syscall_arg(1));
            size_t count = static_cast<size_t>(tf->syscall_arg(2));
            long rc = sys_read(cur, fd, buf, count);
            tf->set_return(static_cast<uint64_t>(rc));
            return true;
        }
        case NR_CLOSE: {
            int fd = static_cast<int>(tf->syscall_arg(0));
            long rc = sys_close(cur, fd);
            tf->set_return(static_cast<uint64_t>(rc));
            return true;
        }
        case NR_WRITE: {
            int fd = static_cast<int>(tf->syscall_arg(0));
            const auto* buf = reinterpret_cast<const char*>(tf->syscall_arg(1));
            size_t count = static_cast<size_t>(tf->syscall_arg(2));
            long rc = sys_write(cur, fd, buf, count);
            tf->set_return(static_cast<uint64_t>(rc));
            return true;
        }
        default: return false;
    }
}

}  // namespace trap

extern "C" void trap_dispatch(TrapFrame* tf) {
    if (!tf) {
        return;
    }

    if (trap::arch_try_handle_irq(tf)) {
        trap::arch_post_dispatch(tf);
    } else if (trap::arch_is_page_fault(tf)) {
        uint32_t err = trap::arch_page_fault_error(tf);
        uintptr_t fault_addr = trap::arch_page_fault_addr(tf);
        if (trap::handle_page_fault(tf, err, fault_addr) != 0) {
            if (err & 4) {
                sched::exit(-1);
            }
            panic("unrecoverable kernel page fault at 0x%lx", fault_addr);
        }
        trap::arch_post_dispatch(tf);
    } else if (trap::arch_is_syscall(tf)) {
        trap::arch_on_syscall_entry(tf);
        if (!trap::handle_syscall(tf)) {
            int nr = static_cast<int>(tf->syscall_nr());
            cprintf("unknown syscall %d\n", nr);
            tf->set_return(static_cast<uint64_t>(-1));
        }
        trap::arch_post_dispatch(tf);
    } else {
        trap::arch_on_unhandled(tf);
        trap::arch_post_dispatch(tf);
    }

    TaskStruct* cur = sched::current();
    if (cur && cur->need_resched) {
        sched::schedule();
    }
}
