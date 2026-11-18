#include "pty.h"
#include "drivers/intr.h"
#include "fs/readiness.h"
#include "sched/sched.h"
#include <sys/array.hpp>

namespace pty {
namespace {

bool valid_size(TerminalSize size) {
    return size.rows != 0 && size.cols != 0 && size.rows <= 4096 && size.cols <= 4096;
}

template<size_t CAPACITY>
class Bytes {
public:
    size_t size() const { return count_; }
    size_t space() const { return CAPACITY - count_; }
    void clear() { count_ = 0; }
    void push(char c) {
        data_[(head_ + count_) % CAPACITY] = c;
        ++count_;
    }
    char pop() {
        char c = data_[head_];
        head_ = (head_ + 1) % CAPACITY;
        --count_;
        return c;
    }
    bool has(char c) const {
        for (size_t i = 0; i < count_; ++i)
            if (data_[(head_ + i) % CAPACITY] == c)
                return true;
        return false;
    }

private:
    sys::array<char, CAPACITY> data_{};
    size_t head_{};
    size_t count_{};
};

}  // namespace

class Session {
public:
    Session(int owner, TerminalSize size) : owner_pid_(owner), size_(size) {}

    SessionRef share() {
        intr::Guard guard;
        ++references_;
        return SessionRef(this);
    }

    TerminalSize size() const {
        intr::Guard guard;
        return size_;
    }
    Error resize(TerminalSize size) {
        ENSURE(valid_size(size));
        intr::Guard guard;
        ENSURE(!closed_, Error::Io);
        size_ = size;
        return Error::None;
    }
    bool output_ready() const { return output_.size() != 0 || closed_; }
    bool input_ready() const { return input_.space() != 0 || closed_; }

    Error attach(Task* parent, Task* child, bool foreground) {
        ENSURE(!closed_ && !parent->termination_requested(), Error::Interrupted);
        if (parent->pty_session() != this) {
            ENSURE(parent->pid == owner_pid_ && controller_pid_ == 0 && !foreground, Error::Access);
            controller_pid_ = foreground_pid_ = child->pid;
        } else if (parent->console_job() != 0 || (foreground && job_owner_pid_ != 0)) {
            ENSURE(parent->console_job() == job_owner_pid_ || parent->pid == job_owner_pid_, Error::Access);
            ENSURE(!job_cancelled_, Error::Interrupted);
            child->set_console_job(job_owner_pid_);
        } else if (foreground) {
            ENSURE(parent->pid == foreground_pid_, Error::Access);
            TRY(check_interrupt());
            if (raw_owner_pid_ == parent->pid)
                release_raw();
            foreground_pid_ = child->pid;
        }
        child->set_pty_session(share());
        return Error::None;
    }

    void task_exited(Task* task) {
        if (task->pid == controller_pid_) {
            hangup(task->pid);
            return;
        }
        if (task->pid == job_owner_pid_) {
            TaskManager::interrupt_job(job_owner_pid_);
            job_owner_pid_ = 0;
            job_cancelled_ = job_interrupted_ = false;
        }
        if (task->console_job() != 0)
            job_done_.wakeup_all();
        if (task->pid == raw_owner_pid_)
            release_raw();
        if (task->pid == foreground_pid_) {
            Task* parent = task->parent;
            foreground_pid_ = parent && parent->pty_session() == this ? parent->pid : controller_pid_;
        }
    }

    void hangup(int except_pid = 0) {
        intr::Guard guard;
        if (closed_)
            return;
        closed_ = true;
        input_ready_.wakeup_all();
        output_space_.wakeup_all();
        job_done_.wakeup_all();
        fd::Readiness::notify();
        TaskManager::interrupt_pty(this, except_pid);
    }

    Error raw(bool enabled) {
        intr::Guard guard;
        Task* task = sched::current();
        ENSURE(!closed_ && task->pty_session() == this && is_foreground(task), Error::Access);
        ENSURE(raw_owner_pid_ == 0 || raw_owner_pid_ == task->pid, Error::Busy);
        if (enabled)
            raw_owner_pid_ = task->pid;
        else if (raw_owner_pid_ != 0)
            release_raw();
        return Error::None;
    }

    Error job(unsigned int action) {
        intr::Guard guard;
        Task* task = sched::current();
        ENSURE(!closed_ && task->pty_session() == this, Error::Access);
        if (action == TERMINAL_JOB_BEGIN) {
            ENSURE(task->pid == foreground_pid_, Error::Access);
            ENSURE(job_owner_pid_ == 0 && raw_owner_pid_ == 0, Error::Busy);
            TRY(check_interrupt());
            job_owner_pid_ = task->pid;
            job_cancelled_ = job_interrupted_ = false;
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
            ENSURE(!closed_ && !task->termination_requested(), Error::Interrupted);
            job_done_.sleep();
        }
        bool interrupted = job_interrupted_;
        job_owner_pid_ = 0;
        job_cancelled_ = job_interrupted_ = false;
        return interrupted ? Error::Interrupted : Error::None;
    }

    Result<int> read_input(void* buffer, size_t count) {
        intr::Guard guard;
        Task* task = sched::current();
        while (true) {
            ENSURE(!task->termination_requested(), Error::Interrupted);
            if (closed_)
                return 0;
            ENSURE(task->pty_session() == this && is_foreground(task) && task->pid != job_owner_pid_ &&
                       (raw_owner_pid_ == 0 || raw_owner_pid_ == task->pid),
                   Error::Access);
            if (input_.size() != 0) {
                bool was_full = input_.space() == 0;
                int taken = take(input_, buffer, count);
                if (was_full)
                    fd::Readiness::notify();
                return taken;
            }
            input_ready_.sleep();
        }
    }

    Result<int> write_input(const void* buffer, size_t count) {
        intr::Guard guard;
        ENSURE(!closed_, Error::Io);
        const auto* bytes = static_cast<const char*>(buffer);
        // Ctrl-C has priority even when input is already full.
        if (count == 1 && bytes[0] == 3) {
            input_.clear();
            fd::Readiness::notify();
            if (job_owner_pid_ != 0 && raw_owner_pid_ == 0) {
                job_cancelled_ = job_interrupted_ = true;
                TaskManager::interrupt_job(job_owner_pid_);
                return 1;
            }
            if (foreground_pid_ != controller_pid_ && raw_owner_pid_ != foreground_pid_) {
                static_cast<void>(TaskManager::interrupt_console(foreground_pid_));
                return 1;
            }
        }
        ENSURE(count <= input_.space(), Error::Busy);
        for (size_t i = 0; i < count; ++i)
            input_.push(bytes[i]);
        input_ready_.wakeup_all();
        return static_cast<int>(count);
    }

    Result<int> read_output(void* buffer, size_t count) {
        intr::Guard guard;
        if (output_.size() == 0) {
            if (closed_)
                return 0;
            return Error::Busy;
        }
        int taken = take(output_, buffer, count);
        output_space_.wakeup_all();
        return taken;
    }

    Result<int> write_output(const void* buffer, size_t count) {
        intr::Guard guard;
        Task* task = sched::current();
        while (output_.space() == 0) {
            ENSURE(!closed_ && !task->termination_requested(), Error::Interrupted);
            output_space_.sleep();
        }
        ENSURE(!closed_ && !task->termination_requested(), Error::Interrupted);
        bool notify = output_.size() == 0;
        size_t written = count < output_.space() ? count : output_.space();
        auto* bytes = static_cast<const char*>(buffer);
        for (size_t i = 0; i < written; ++i)
            output_.push(bytes[i]);
        if (notify)
            fd::Readiness::notify();
        return static_cast<int>(written);
    }

private:
    friend struct Release;
    size_t references_{1};
    int owner_pid_;
    TerminalSize size_;
    int controller_pid_{};
    int foreground_pid_{};
    int job_owner_pid_{};
    int raw_owner_pid_{};
    bool closed_{};
    bool job_cancelled_{};
    bool job_interrupted_{};
    Bytes<256> input_;
    Bytes<4096> output_;
    WaitQueue input_ready_;
    WaitQueue output_space_;
    WaitQueue job_done_;

    bool is_foreground(const Task* task) const {
        return task->pid == foreground_pid_ || (job_owner_pid_ != 0 && task->console_job() == job_owner_pid_);
    }
    Error check_interrupt() {
        if (!input_.has(3))
            return Error::None;
        input_.clear();
        fd::Readiness::notify();
        return Error::Interrupted;
    }
    void release_raw() {
        raw_owner_pid_ = 0;
        input_.clear();
        fd::Readiness::notify();
        input_ready_.wakeup_all();
    }
    template<size_t CAPACITY>
    static int take(Bytes<CAPACITY>& queue, void* buffer, size_t count) {
        size_t taken = count < queue.size() ? count : queue.size();
        auto* bytes = static_cast<char*>(buffer);
        for (size_t i = 0; i < taken; ++i)
            bytes[i] = queue.pop();
        return static_cast<int>(taken);
    }
};

void Release::operator()(Session* session) const noexcept {
    intr::Guard guard;
    if (--session->references_ == 0)
        delete session;
}

namespace {

class Endpoint : public vfs::File {
public:
    Endpoint(SessionRef session, bool master) : session_(sys::move(session)), master_(master) {}
    ~Endpoint() override {
        if (master_)
            session_->hangup();
    }
    bool is_stream() const override { return true; }
    bool is_duplex() const override { return true; }
    Session* controlling_terminal() override { return master_ ? nullptr : session_.get(); }
    Result<bool> read_ready() override {
        ENSURE(master_, Error::NotSupported);
        return session_->output_ready();
    }
    Result<bool> write_ready() override {
        ENSURE(master_, Error::NotSupported);
        return session_->input_ready();
    }
    Result<int> read(void* buffer, size_t count, size_t) override {
        return master_ ? session_->read_output(buffer, count) : session_->read_input(buffer, count);
    }
    Result<int> write(const void* buffer, size_t count, size_t) override {
        return master_ ? session_->write_input(buffer, count) : session_->write_output(buffer, count);
    }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::CharDevice, 0, 0);
        return Error::None;
    }
    Error control(unsigned int request, void* value) override {
        if (request == TERMINAL_SET_SIZE && master_)
            return session_->resize(*static_cast<const TerminalSize*>(value));
        ENSURE(!master_, Error::NotSupported);
        if (request == TERMINAL_GET_SIZE) {
            *static_cast<TerminalSize*>(value) = session_->size();
            return Error::None;
        }
        if (request == TERMINAL_SET_RAW)
            return session_->raw(*static_cast<const unsigned int*>(value) != 0);
        if (request == TERMINAL_JOB)
            return session_->job(*static_cast<const unsigned int*>(value));
        return Error::NotSupported;
    }

private:
    SessionRef session_;
    bool master_;
};

}  // namespace

Error open(Task* task, int (&ends)[2], TerminalSize size) {
    ENSURE(valid_size(size));
    ENSURE(task->files().size() <= fd::MAX_FD - 2, Error::Full);
    SessionRef session(new (sys::nothrow) Session(task->pid, size));
    ENSURE(session, Error::NoMem);
    vfs::FileHandle master(new (sys::nothrow) Endpoint(session->share(), true));
    ENSURE(master, Error::NoMem);
    vfs::FileHandle slave(new (sys::nothrow) Endpoint(session->share(), false));
    ENSURE(slave, Error::NoMem);
    int reader = TRY(task->files().alloc(sys::move(master), true));
    auto writer = task->files().alloc(sys::move(slave), true);
    if (!writer.ok()) {
        static_cast<void>(task->files().close(reader));
        return writer.error();
    }
    ends[0] = reader;
    ends[1] = writer.value();
    return Error::None;
}

Result<bool> prepare_spawn(Task* parent, Task* child, bool foreground) {
    if (!parent)
        return false;
    auto* session = parent->pty_session();
    if (!session) {
        auto* entry = child->files().get(0);
        session = entry ? entry->file()->controlling_terminal() : nullptr;
    }
    if (!session)
        return false;
    TRY(session->attach(parent, child, foreground));
    return true;
}

void task_exited(Task* task) {
    task->pty_session()->task_exited(task);
}

}  // namespace pty
