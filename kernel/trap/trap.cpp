#include "trap/trap.h"

#include "lib/unistd.h"
#include "lib/stdio.h"

#include <asm/page.h>

#include "drivers/fbcons.h"
#include "cons/cons.h"
#include "fs/vfs.h"
#include "mm/vmm.h"
#include "sched/sched.h"

namespace timer {
extern volatile int64_t ticks;
}

namespace {

constexpr size_t SYSCALL_PATH_MAX = 128;

bool user_range_valid(uintptr_t addr, size_t size) {
    if (addr >= USER_SPACE_TOP) {
        return false;
    }
    if (size > USER_SPACE_TOP - addr) {
        return false;
    }
    return true;
}

int copy_user_cstr(const char* user, char* out, size_t out_size) {
    if (!user || !out || out_size == 0) {
        return -1;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(user);
    if (base >= USER_SPACE_TOP) {
        return -1;
    }

    for (size_t i = 0; i < out_size; i++) {
        uintptr_t cur = base + i;
        if (cur >= USER_SPACE_TOP) {
            return -1;
        }

        char ch = user[i];
        out[i] = ch;
        if (ch == '\0') {
            return 0;
        }
    }

    out[out_size - 1] = '\0';
    return -1;
}

long sys_open(TaskStruct* cur, const char* user_path, int flags, int mode) {
    (void)flags;
    (void)mode;

    if (!cur) {
        return -1;
    }

    char path[SYSCALL_PATH_MAX]{};
    if (copy_user_cstr(user_path, path, sizeof(path)) != 0) {
        return -1;
    }

    vfs::File* file = nullptr;
    if (vfs::open(path, &file) != 0 || !file) {
        return -1;
    }

    int fd = cur->alloc_fd(file);
    if (fd < 0) {
        vfs::close(file);
        return -1;
    }

    return fd;
}

long sys_read(TaskStruct* cur, int fd, void* user_buf, size_t count) {
    if (!cur) {
        return -1;
    }

    if (count == 0) {
        return 0;
    }

    if (!user_buf) {
        return -1;
    }

    uintptr_t buf_addr = reinterpret_cast<uintptr_t>(user_buf);
    if (!user_range_valid(buf_addr, count)) {
        return -1;
    }

    TaskStruct::FdEntry* entry = cur->get_fd(fd);
    if (!entry) {
        return -1;
    }

    int bytes = vfs::read(entry->file, user_buf, count, entry->offset);
    if (bytes < 0) {
        return -1;
    }

    entry->offset += static_cast<size_t>(bytes);
    return bytes;
}

long sys_close(TaskStruct* cur, int fd) {
    if (!cur) {
        return -1;
    }

    return cur->close_fd(fd);
}

long sys_write(const char* user_buf, size_t count) {
    if (count == 0) {
        return 0;
    }

    if (!user_buf) {
        return -1;
    }

    uintptr_t buf_addr = reinterpret_cast<uintptr_t>(user_buf);
    if (!user_range_valid(buf_addr, count)) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        cons::putc(user_buf[i]);
    }

    return static_cast<long>(count);
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
            const auto* buf = reinterpret_cast<const char*>(tf->syscall_arg(1));
            size_t count = static_cast<size_t>(tf->syscall_arg(2));
            long rc = sys_write(buf, count);
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
        trap::handle_page_fault(tf, err, fault_addr);
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
