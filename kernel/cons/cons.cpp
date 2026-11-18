#include "cons/cons.h"
#include "cons/terminal.h"
#include "cons/keyboard.h"
#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>
#include "sched/sched.h"

namespace {

class Console {
public:
    static void putc(int c, bool from_file) {
        intr::Guard guard;
        Task* task = sched::current();
        if (raw_owner_pid_ != 0 && (!from_file || !task || task->pid != raw_owner_pid_)) {
            if (deferred_count_ < deferred_.size())
                deferred_[deferred_count_++] = static_cast<char>(c);
            else
                deferred_overflow_ = true;
            return;
        }
        cons::arch_putc(c);
        terminal::putc(c);
    }

    static void emergency_restore() {
        intr::Guard guard;
        release_raw();
    }

    static void push_input(char c) {
        intr::Guard guard;
        // Normalize the console stream once, including across reader handoff.
        // Otherwise a serial CRLF can leave LF for the newly spawned reader.
        if (c == '\n' && skip_lf_) {
            skip_lf_ = false;
            return;
        }
        skip_lf_ = c == '\r';
        if (c == '\r')
            c = '\n';
        if (c == 3) {
            // Discard pending input on interruption, even when the ring is full.
            read_ = write_;
            if (job_owner_pid_ != 0 && raw_owner_pid_ == 0) {
                job_cancelled_ = true;
                job_interrupted_ = true;
                TaskManager::interrupt_job(job_owner_pid_);
                return;
            }
            if (foreground_pid_ != 0 && foreground_pid_ != controller_pid_ && raw_owner_pid_ != foreground_pid_) {
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

    static void push_key(int key) {
        if (key < static_cast<int>(cons::Key::Up)) {
            if (key >= 0)
                push_input(static_cast<char>(key));
            return;
        }
        const char* sequence{};
        switch (static_cast<cons::Key>(key)) {
            case cons::Key::Up: sequence = "\x1b[A"; break;
            case cons::Key::Down: sequence = "\x1b[B"; break;
            case cons::Key::Right: sequence = "\x1b[C"; break;
            case cons::Key::Left: sequence = "\x1b[D"; break;
            case cons::Key::Home: sequence = "\x1b[H"; break;
            case cons::Key::End: sequence = "\x1b[F"; break;
            case cons::Key::Delete: sequence = "\x1b[3~"; break;
        }
        if (!sequence)
            return;
        intr::Guard guard;
        size_t length = strlen(sequence);
        size_t used = (write_ + input_.size() - read_) % input_.size();
        if (length >= input_.size() - used)
            return;  // Do not leave a partial escape sequence in a full ring.
        while (*sequence)
            push_input(*sequence++);
    }

    static Error set_raw(bool enabled) {
        intr::Guard guard;
        Task* task = sched::current();
        ENSURE(task && is_foreground(task) && task != TaskManager::init_task() && task->memory() &&
                   task->memory() != &vmm::Manager::kernel_mm(),
               Error::Access);
        if (enabled) {
            ENSURE(raw_owner_pid_ == 0 || raw_owner_pid_ == task->pid, Error::Busy);
            raw_owner_pid_ = task->pid;
            waitq_.wakeup_all();
        } else {
            ENSURE(raw_owner_pid_ == 0 || raw_owner_pid_ == task->pid, Error::Access);
            release_raw();
        }
        return Error::None;
    }

    static Result<char> read_input() {
        while (true) {
            // Keep checking the buffer and registering the waiter atomic.
            intr::Guard guard;
            Task* task = sched::current();
            ENSURE(!task || !task->termination_requested(), Error::Interrupted);
            ENSURE(!task || (job_owner_pid_ == 0 && foreground_pid_ == 0) ||
                       (is_foreground(task) && task->pid != job_owner_pid_),
                   Error::Access);
            ENSURE(!task || raw_owner_pid_ == 0 || task->pid == raw_owner_pid_, Error::Access);
            if (read_ != write_) {
                char c = input_[read_];
                read_ = (read_ + 1) % input_.size();
                return c;
            }
            waitq_.sleep();
        }
    }

    static Error control_job(unsigned int action) {
        Task* task = sched::current();
        ENSURE(task && task != TaskManager::init_task() && task->memory() &&
                   task->memory() != &vmm::Manager::kernel_mm(),
               Error::Access);
        intr::Guard guard;
        if (action == TERMINAL_JOB_BEGIN) {
            ENSURE(task->pid == foreground_pid_, Error::Access);
            ENSURE(job_owner_pid_ == 0 && raw_owner_pid_ == 0, Error::Busy);
            TRY(check_interrupt(task));
            job_owner_pid_ = task->pid;
            job_cancelled_ = false;
            job_interrupted_ = false;
            return Error::None;
        }
        ENSURE(job_owner_pid_ == task->pid, Error::Access);
        if (action == TERMINAL_JOB_CANCEL) {
            job_cancelled_ = true;
            TaskManager::interrupt_job(job_owner_pid_);
            return Error::None;
        }
        ENSURE(action == TERMINAL_JOB_END, Error::Invalid);
        while (TaskManager::has_live_job(job_owner_pid_)) {
            ENSURE(!task->termination_requested(), Error::Interrupted);
            job_done_.sleep();
        }
        bool interrupted = job_interrupted_;
        job_owner_pid_ = 0;
        job_cancelled_ = false;
        job_interrupted_ = false;
        return interrupted ? Error::Interrupted : Error::None;
    }

    static Error prepare_spawn(Task* parent, Task* child, bool foreground) {
        if (parent && parent->console_job() != 0) {
            ENSURE(parent->console_job() == job_owner_pid_, Error::Access);
            ENSURE(!parent->termination_requested() && !job_cancelled_, Error::Interrupted);
            child->set_console_job(job_owner_pid_);
            return Error::None;
        }
        if (!foreground)
            return Error::None;
        ENSURE(parent && child, Error::Invalid);
        if (job_owner_pid_ != 0) {
            ENSURE(parent->pid == job_owner_pid_, Error::Access);
            ENSURE(!parent->termination_requested() && !job_cancelled_, Error::Interrupted);
            child->set_console_job(job_owner_pid_);
            return Error::None;
        }
        return handoff(parent, child);
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
        TRY(check_interrupt(parent));
        if (controller_pid_ == 0)
            controller_pid_ = child->pid;
        if (raw_owner_pid_ == parent->pid)
            release_raw();
        foreground_pid_ = child->pid;
        return Error::None;
    }

    static void task_exited(Task* task) {
        if (task->pid == job_owner_pid_) {
            TaskManager::interrupt_job(job_owner_pid_);
            job_owner_pid_ = 0;
            job_cancelled_ = false;
            job_interrupted_ = false;
        }
        if (task->console_job() != 0)
            job_done_.wakeup_all();
        if (task->pid == raw_owner_pid_)
            release_raw();
        if (task->pid == controller_pid_) {
            controller_pid_ = 0;
            foreground_pid_ = 0;
        } else if (task->pid == foreground_pid_) {
            Task* parent = task->parent;
            foreground_pid_ = parent && parent != TaskManager::init_task() ? parent->pid : controller_pid_;
        }
    }

private:
    static bool is_foreground(Task* task) {
        return task->pid == foreground_pid_ || (job_owner_pid_ != 0 && task->console_job() == job_owner_pid_);
    }
    static Error check_interrupt(Task* task) {
        ENSURE(!task->termination_requested(), Error::Interrupted);
        for (size_t i = read_; i != write_; i = (i + 1) % input_.size()) {
            if (input_[i] == 3) {
                read_ = write_;
                return Error::Interrupted;
            }
        }
        return Error::None;
    }
    static void release_raw() {
        if (raw_owner_pid_ != 0) {
            raw_owner_pid_ = 0;
            waitq_.wakeup_all();
            read_ = write_;  // Application keystrokes must not become shell commands.
            skip_lf_ = false;
            terminal::restore();
            size_t count = deferred_count_;
            deferred_count_ = 0;
            for (size_t i = 0; i < count; ++i)
                putc(deferred_[i], false);
            if (deferred_overflow_) {
                deferred_overflow_ = false;
                for (const char* c = "[console output truncated]\n"; *c; ++c)
                    putc(*c, false);
            }
        }
    }
    inline static sys::array<char, 1024> deferred_{};
    inline static size_t deferred_count_{};
    inline static bool deferred_overflow_{};
    inline static int job_owner_pid_{};
    inline static bool job_cancelled_{};
    inline static bool job_interrupted_{};
    inline static WaitQueue job_done_{};
    inline static int raw_owner_pid_{};
    inline static bool skip_lf_{};
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
void push_key(int key) {
    Console::push_key(key);
}
Error set_raw(bool enabled) {
    return Console::set_raw(enabled);
}
void emergency_restore() {
    Console::emergency_restore();
}
TerminalSize screen_size() {
    return terminal::size();
}
Error prepare_spawn(Task* parent, Task* child, bool foreground) {
    return Console::prepare_spawn(parent, child, foreground);
}
Error control_job(unsigned int action) {
    return Console::control_job(action);
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
    Console::putc(c, false);
}
void write_output(int c) {
    Console::putc(c, true);
}

}  // namespace cons
