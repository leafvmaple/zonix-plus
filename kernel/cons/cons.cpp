#include "cons/cons.h"
#include "cons/job_control.h"
#include "cons/terminal.h"
#include <base/keyboard.h>
#include "drivers/fbcons.h"
#include "drivers/display.h"
#include "drivers/intr.h"
#include "fs/readiness.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>
#include "sched/sched.h"
#include "trap/trap.h"

namespace {

class Console {
public:
    static void putc(int c, bool from_file) {
        intr::Guard guard;
        Task* task = sched::current();
        if (display_file_ || (raw_owner_pid_ != 0 && (!from_file || !task || task->pid != raw_owner_pid_))) {
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
        release_display(display_file_);
        release_raw();
    }

    static void push_input(char c) {
        intr::Guard guard;
        if (display_events_ && c != 3)
            return;
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
            if (job_.active() && raw_owner_pid_ == 0) {
                job_.interrupt();
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
        if (read_ == write_)
            fd::Readiness::notify();
        input_[write_] = c;
        write_ = next;
        waitq_.wakeup_one();
    }

    static void key_event(uint16_t code, int value) {
        intr::Guard guard;
        int key = keyboard_.translate(code, value != DISPLAY_KEY_RELEASE);
        if (key == 3 && !display_ctrl_keys_)
            push_input(3);
        else if (display_events_)
            queue_event({DISPLAY_EVENT_KEY, code, value, keyboard_.modifiers()});
        else
            push_key(key);
    }
    static void pointer_event(unsigned int type, unsigned int code, int value) {
        intr::Guard guard;
        if (display_events_)
            queue_event({type, code, value, keyboard_.modifiers()});
    }

    static void push_key(int key) {
        if (key < static_cast<int>(keyboard::Key::Up)) {
            if (key >= 0)
                push_input(static_cast<char>(key));
            return;
        }
        const char* sequence = keyboard::sequence(key);
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
        ENSURE(!display_file_, Error::Busy);
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

    static Error acquire_display(vfs::File* owner, unsigned int flags) {
        intr::Guard guard;
        Task* task = sched::current();
        ENSURE(task && task != TaskManager::init_task() && task->memory() &&
                   task->memory() != &vmm::Manager::kernel_mm() && task->pid == foreground_pid_,
               Error::Access);
        ENSURE(!display_file_ && raw_owner_pid_ == 0 && !job_.active(), Error::Busy);
        ENSURE(fbcons::is_active(), Error::NoDevice);
        ENSURE(!task->termination_requested(), Error::Interrupted);
        display_file_ = owner;
        display_owner_pid_ = task->pid;
        display_events_ = (flags & DISPLAY_ACQUIRE_EVENTS) != 0;
        display_ctrl_keys_ = (flags & DISPLAY_ACQUIRE_KEYBOARD_CTRL_C) != 0;
        if (display_events_) {
            read_ = write_;
            queue_event({DISPLAY_EVENT_RESET, 0, 0, keyboard_.modifiers()});
        }
        fbcons::suspend();
        return Error::None;
    }
    static Result<bool> display_event_ready(const vfs::File* owner) {
        ENSURE(display_events_ && owner == display_file_ && sched::current()->pid == display_owner_pid_, Error::Access);
        return events_read_ != events_write_;
    }
    static Error read_display_event(const vfs::File* owner, DisplayTimedEvent& event) {
        intr::Guard guard;
        Task* task = sched::current();
        while (true) {
            ENSURE(!task->termination_requested(), Error::Interrupted);
            if (TRY(display_event_ready(owner))) {
                event = events_[events_read_];
                events_read_ = (events_read_ + 1) % events_.size();
                return Error::None;
            }
            events_ready_.sleep();
        }
    }

    static bool owns_display(const vfs::File* owner) {
        intr::Guard guard;
        return owner && owner == display_file_;
    }
    static void release_display(vfs::File* owner) {
        intr::Guard guard;
        if (!owner || display_file_ != owner)
            return;
        display_file_ = nullptr;
        display_owner_pid_ = 0;
        display_events_ = false;
        display_ctrl_keys_ = false;
        events_read_ = events_write_;
        events_ready_.wakeup_all();
        fd::Readiness::notify();
        fbcons::resume();
        restore_console();
    }

    // Called with interrupts disabled; the same rules decide reading and readiness.
    static Error check_reader(Task* task) {
        ENSURE(!task || (!job_.active() && foreground_pid_ == 0) || (is_foreground(task) && task->pid != job_.owner()),
               Error::Access);
        ENSURE(!display_events_ && (!task || raw_owner_pid_ == 0 || task->pid == raw_owner_pid_), Error::Access);
        return Error::None;
    }

    static Result<bool> input_ready() {
        intr::Guard guard;
        TRY(check_reader(sched::current()));
        return read_ != write_;
    }

    static Result<char> read_input() {
        while (true) {
            // Keep checking the buffer and registering the waiter atomic.
            intr::Guard guard;
            Task* task = sched::current();
            ENSURE(!task || !task->termination_requested(), Error::Interrupted);
            TRY(check_reader(task));
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
            ENSURE(!job_.active() && raw_owner_pid_ == 0 && !display_file_, Error::Busy);
            TRY(check_interrupt(task));
            job_.begin(task->pid);
            return Error::None;
        }
        ENSURE(job_.owner() == task->pid, Error::Access);
        if (action == TERMINAL_JOB_CANCEL) {
            job_.cancel();
            return Error::None;
        }
        ENSURE(action == TERMINAL_JOB_END, Error::Invalid);
        return job_.end([task] { return task->termination_requested(); });
    }

    static Error prepare_spawn(Task* parent, Task* child, bool foreground) {
        if (parent && parent->console_job() != 0) {
            ENSURE(parent->console_job() == job_.owner(), Error::Access);
            ENSURE(!parent->termination_requested() && !job_.cancelled(), Error::Interrupted);
            child->set_console_job(job_.owner());
            return Error::None;
        }
        if (!foreground)
            return Error::None;
        ENSURE(parent && child, Error::Invalid);
        if (job_.active()) {
            ENSURE(parent->pid == job_.owner(), Error::Access);
            ENSURE(!parent->termination_requested() && !job_.cancelled(), Error::Interrupted);
            child->set_console_job(job_.owner());
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
        if (display_owner_pid_ == parent->pid)
            release_display(display_file_);
        foreground_pid_ = child->pid;
        return Error::None;
    }

    static void task_exited(Task* task) {
        if (task->pid == display_owner_pid_)
            release_display(display_file_);
        job_.task_exited(task);
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
    static void queue_event(const DisplayEvent& event) {
        size_t next = (events_write_ + 1) % events_.size();
        auto timestamp_ms = static_cast<long>(trap::uptime_ms());
        if (next == events_read_) {
            events_read_ = events_write_;
            events_[events_write_] = {{DISPLAY_EVENT_RESET, 0, 0, event.modifiers}, timestamp_ms};
        } else {
            events_[events_write_] = {event, timestamp_ms};
        }
        if (events_read_ == events_write_)
            fd::Readiness::notify();
        events_write_ = next;
        events_ready_.wakeup_one();
    }

    static bool is_foreground(Task* task) { return task->pid == foreground_pid_ || job_.includes(task); }
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
            restore_console();
        }
    }
    static void restore_console() {
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
    inline static keyboard::KeyboardState keyboard_{};
    inline static sys::array<DisplayTimedEvent, 64> events_{};
    inline static size_t events_read_{};
    inline static size_t events_write_{};
    inline static WaitQueue events_ready_{};
    inline static bool display_events_{};
    inline static bool display_ctrl_keys_{};
    inline static vfs::File* display_file_{};
    inline static int display_owner_pid_{};
    inline static sys::array<char, 1024> deferred_{};
    inline static size_t deferred_count_{};
    inline static bool deferred_overflow_{};
    inline static JobControl job_{};
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
    int result = display::init();
    fbcons::late_init();
    return result;
}
void push_input(char c) {
    Console::push_input(c);
}
void key_event(uint16_t code, int value) {
    Console::key_event(code, value);
}
void pointer_event(unsigned int type, unsigned int code, int value) {
    Console::pointer_event(type, code, value);
}
Error set_raw(bool enabled) {
    return Console::set_raw(enabled);
}
Error acquire_display(vfs::File* owner, unsigned int flags) {
    return Console::acquire_display(owner, flags);
}
Error read_display_event(const vfs::File* owner, DisplayTimedEvent& event) {
    return Console::read_display_event(owner, event);
}
Result<bool> display_event_ready(const vfs::File* owner) {
    return Console::display_event_ready(owner);
}
void release_display(vfs::File* owner) {
    Console::release_display(owner);
}
bool owns_display(const vfs::File* owner) {
    return Console::owns_display(owner);
}
void emergency_restore() {
    Console::emergency_restore();
}
TerminalSize screen_size() {
    return terminal::size();
}
Error prepare_spawn(Task* parent, Task* child, bool foreground) {
    if (TRY(pty::prepare_spawn(parent, child, foreground)))
        return Error::None;
    return Console::prepare_spawn(parent, child, foreground);
}
Error control_job(unsigned int action) {
    return Console::control_job(action);
}
void task_exited(Task* task) {
    if (task->pty_session())
        pty::task_exited(task);
    else
        Console::task_exited(task);
}
char getc() {
    return Console::read_input().value_or(0);
}
Result<char> read_input() {
    return Console::read_input();
}
Result<bool> input_ready() {
    return Console::input_ready();
}
void putc(int c) {
    Console::putc(c, false);
}
void write_output(int c) {
    Console::putc(c, true);
}

}  // namespace cons
