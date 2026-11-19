#include "client.h"
#include "../command.h"

#include "fd.h"

namespace desktop_ipc {

Client::~Client() {
    stop();
}

long Client::start(int index, const char* edit_path) {
    index_ = index;
    const auto& spec = WINDOWS[index];
    auto* path = const_cast<char*>(spec.program);
    auto* argument = const_cast<char*>(index == EDITOR_WINDOW ? edit_path : spec.argument);
    char* argv[] = {path, argument, nullptr};

    // Desktop reads input from its display FD. Keep descriptor 0 occupied
    // using diagnostics as the placeholder rather than saving an unused FD.
    if (sys_dup2(STDERR_FD, STDIN_FD) < 0)
        return SYS_ERROR_IO;
    Fd saved_out(sys_fcntl(STDOUT_FD, F_DUPFD_CLOEXEC, 0));
    if (saved_out.get() < 0)
        return SYS_ERROR_FULL;

    int ends[2];
    long error = sys_pipe2(ends, O_CLOEXEC | O_NONBLOCK);
    if (error < 0)
        return error;
    Fd parent_out(ends[1]);
    {
        Fd child_in(ends[0]);
        error = sys_dup2(child_in.get(), STDIN_FD);
    }
    if (error < 0)
        return error;

    // The extra copy of the child input is already closed. At most four
    // temporary slots are needed when launching another window.
    error = sys_pipe2(ends, index >= TERMINAL_WINDOW ? O_CLOEXEC : O_CLOEXEC | O_NONBLOCK);
    if (error < 0) {
        sys_dup2(STDERR_FD, STDIN_FD);
        return error;
    }
    Fd parent_in(ends[0]);
    long pid;
    {
        Fd child_out(ends[1]);
        pid = sys_dup2(child_out.get(), STDOUT_FD);
    }
    if (pid >= 0)
        pid = sys_spawn(path, argument ? 2 : 1, argv);
    long restored_in = sys_dup2(STDERR_FD, STDIN_FD);
    long restored_out = sys_dup2(saved_out.get(), STDOUT_FD);
    if (pid < 0)
        return pid;
    pid_ = static_cast<int>(pid);
    input_fd_ = parent_out.release();
    output_fd_ = parent_in.release();
    // Every client sends one initial frame before consuming input.
    waiting_ = true;
    if (restored_in < 0 || restored_out < 0) {
        stop();
        return SYS_ERROR_IO;
    }
    return 0;
}

void Client::stop() {
    if (!alive())
        return;
    sys_close(input_fd_);
    sys_close(output_fd_);
    sys_kill(pid_);
    sys_wait(pid_, nullptr);
    pid_ = 0;
    used_ = head_ = count_ = 0;
    waiting_ = false;
    if (!is_pixel_window(index_))
        buffers().frame = {};
}

unsigned int Client::mask() const {
    return alive() ? 1U << output_fd_ : 0;
}

bool Client::receive_frame() {
    if (!waiting_ && !is_text_window(index_))
        return false;  // Unsolicited frame after initialization.
    damage_ = {};
    auto& state = buffers();
    auto* bytes = reinterpret_cast<char*>(state.pending.data());
    long count = sys_read(output_fd_, bytes + used_, sizeof(Frame) - used_);
    if (count <= 0)
        return false;
    used_ += static_cast<unsigned int>(count);
    if (used_ == sizeof(Frame)) {
        if (is_text_window(index_))
            damage_ = compare_text(state.frame, state.pending);
        state.frame = state.pending;
        used_ = 0;
        waiting_ = false;
    }
    return flush();
}

bool Client::send(const Input& input) {
    // Stream clients send updates independently of input. Blocking output
    // applies backpressure to the client while Desktop input stays nonblocking.
    if (index_ >= TERMINAL_WINDOW)
        return sys_write(input_fd_, const_cast<Input*>(&input), sizeof(Input)) == sizeof(Input);
    if (count_ == QUEUE_SIZE) {
        head_ = 0;
        count_ = 1;
        queue_[0] = {{DISPLAY_EVENT_RESET, 0, 0, input.event.modifiers}, input.x, input.y};
    }
    queue_[(head_ + count_) % QUEUE_SIZE] = input;
    ++count_;
    return flush();
}

bool Client::flush() {
    if (waiting_ || count_ == 0)
        return true;
    if (sys_write(input_fd_, &queue_[head_], sizeof(Input)) != sizeof(Input))
        return false;
    head_ = (head_ + 1) % QUEUE_SIZE;
    --count_;
    waiting_ = true;
    return true;
}

}  // namespace desktop_ipc
