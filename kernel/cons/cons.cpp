#include "cons/cons.h"
#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>
#include "sched/sched.h"

namespace {

class Console {
public:
    static void push_input(char c) {
        intr::Guard guard;
        if (c == 3) {
            // Discard pending input on interruption, even when the ring is full.
            read_ = write_;
            if (foreground_pid_ != 0 && foreground_pid_ != controller_pid_) {
                static_cast<void>(TaskManager::interrupt_console(foreground_pid_));
                return;
            }
        }
        size_t next = (write_ + 1) % input_.size();
        if (next == read_) {
            return;
        }
        input_[write_] = c;
        write_ = next;
        waitq_.wakeup_one();
    }

    static Result<char> read_input() {
        while (true) {
            // Keep checking the buffer and registering the waiter atomic.
            intr::Guard guard;
            Task* task = sched::current();
            ENSURE(!task || !task->termination_requested(), Error::Interrupted);
            ENSURE(!task || foreground_pid_ == 0 || task->pid == foreground_pid_, Error::Access);
            if (read_ != write_) {
                char c = input_[read_];
                read_ = (read_ + 1) % input_.size();
                return c;
            }
            waitq_.sleep();
        }
    }

    static Error handoff(Task* parent, Task* child) {
        ENSURE(parent && child && child->memory() && child->memory() != &vmm::Manager::kernel_mm(), Error::Access);
        if (controller_pid_ == 0) {
            // Init starts the console controller, but can never be its target.
            ENSURE(parent == TaskManager::init_task(), Error::Access);
        } else {
            ENSURE(parent->pid == foreground_pid_, Error::Access);
        }
        ENSURE(!parent->termination_requested(), Error::Interrupted);
        // A controller may have read the command's newline before a queued
        // Ctrl-C. Cancel the pending spawn rather than giving that byte to a
        // new child which may never read input.
        if (parent->pid == controller_pid_) {
            for (size_t i = read_; i != write_; i = (i + 1) % input_.size()) {
                if (input_[i] == 3) {
                    read_ = write_;
                    return Error::Interrupted;
                }
            }
        }
        if (controller_pid_ == 0)
            controller_pid_ = child->pid;
        foreground_pid_ = child->pid;
        return Error::None;
    }

    static void task_exited(Task* task) {
        if (task->pid == controller_pid_) {
            controller_pid_ = 0;
            foreground_pid_ = 0;
        } else if (task->pid == foreground_pid_) {
            Task* parent = task->parent;
            foreground_pid_ = parent && parent != TaskManager::init_task() ? parent->pid : controller_pid_;
        }
    }

private:
    inline static int controller_pid_{};
    inline static int foreground_pid_{};
    inline static sys::array<char, 128> input_{};
    inline static size_t read_{};
    inline static size_t write_{};
    inline static WaitQueue waitq_{};
};

}  // namespace

namespace cons {

int init() {
    return arch_init();
}
int late_init() {
    fbcons::late_init();
    return 0;
}
void push_input(char c) {
    Console::push_input(c);
}
Error handoff(Task* parent, Task* child) {
    return Console::handoff(parent, child);
}
void task_exited(Task* task) {
    Console::task_exited(task);
}
char getc() {
    return Console::read_input().value_or(0);
}
Result<char> read_input() {
    return Console::read_input();
}
void putc(int c) {
    arch_putc(c);
    fbcons::putc(c);
}

}  // namespace cons
