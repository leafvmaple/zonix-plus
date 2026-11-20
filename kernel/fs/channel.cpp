#include "channel.h"
#include "drivers/intr.h"
#include "fs/pipe.h"
#include "fs/readiness.h"
#include "lib/waitqueue.h"
#include "sched/sched.h"
#include <abi/channel.h>
#include <sys/array.hpp>

namespace channel {

namespace {

constexpr size_t MAX_LISTENERS = 16;

Error check_name(const char* name) {
    size_t length = 0;
    for (; name[length] != '\0'; ++length) {
        ENSURE(length < CHANNEL_NAME_MAX, Error::Invalid);
        ENSURE(name[length] > ' ' && name[length] <= '~', Error::Invalid);
    }
    ENSURE(length != 0, Error::Invalid);
    return Error::None;
}

}  // namespace

class Listener : public vfs::File {
public:
    Listener(const char* name, bool nonblocking) : nonblocking_(nonblocking) {
        for (size_t i = 0; name[i] != '\0'; ++i)
            name_[i] = name[i];
    }
    ~Listener() override;

    bool is_stream() const override { return true; }
    Result<int> read(void*, size_t, size_t) override { return Error::NotSupported; }
    Result<int> write(const void*, size_t, size_t) override { return Error::NotSupported; }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::Pipe, 0, 0);
        return Error::None;
    }
    // Readable while a connected stream waits for accept.
    Result<bool> read_ready() override {
        intr::Guard guard;
        return count_ != 0;
    }
    Error set_nonblocking(bool nonblocking) override {
        intr::Guard guard;
        nonblocking_ = nonblocking;
        return Error::None;
    }
    Listener* channel_listener() override { return this; }

    bool named(const char* name) const { return strcmp(name_, name) == 0; }

    Error enqueue(vfs::FileHandle stream) {
        intr::Guard guard;
        ENSURE(count_ < pending_.size(), Error::Full);
        pending_[(head_ + count_) % pending_.size()] = sys::move(stream);
        ++count_;
        arrived_.wakeup_all();
        fd::Readiness::notify();
        return Error::None;
    }

    Result<vfs::FileHandle> take() {
        intr::Guard guard;
        Task* task = sched::current();
        while (count_ == 0) {
            ENSURE(!nonblocking_, Error::Busy);
            ENSURE(!task->termination_requested(), Error::Interrupted);
            arrived_.sleep();
        }
        vfs::FileHandle stream = sys::move(pending_[head_]);
        head_ = (head_ + 1) % pending_.size();
        --count_;
        return stream;
    }

private:
    char name_[CHANNEL_NAME_MAX + 1]{};
    sys::array<vfs::FileHandle, CHANNEL_BACKLOG> pending_{};
    size_t head_{};
    size_t count_{};
    WaitQueue arrived_{};
    bool nonblocking_;
};

namespace {

// Names are global; a listener holds its name from listen until its file closes.
class Registry {
public:
    static Error add(Listener* listener, const char* name) {
        intr::Guard guard;
        ENSURE(!find(name), Error::Exists);
        for (auto& slot : listeners_) {
            if (!slot) {
                slot = listener;
                return Error::None;
            }
        }
        return Error::Full;
    }

    static void remove(Listener* listener) {
        intr::Guard guard;
        for (auto& slot : listeners_) {
            if (slot == listener)
                slot = nullptr;
        }
    }

    // The caller holds interrupts off while it uses the result.
    static Listener* find(const char* name) {
        for (Listener* listener : listeners_) {
            if (listener && listener->named(name))
                return listener;
        }
        return nullptr;
    }

private:
    inline static sys::array<Listener*, MAX_LISTENERS> listeners_{};
};

}  // namespace

// Streams still queued are closed here, which their connected ends read as end of file.
Listener::~Listener() {
    Registry::remove(this);
}

Result<vfs::FileHandle> listen(const char* name, bool nonblocking) {
    TRY(check_name(name));
    auto* created = new (sys::nothrow) Listener(name, nonblocking);
    ENSURE(created, Error::NoMem);
    vfs::FileHandle listener(created);
    TRY(Registry::add(created, name));
    return listener;
}

Result<vfs::FileHandle> connect(const char* name, bool nonblocking) {
    TRY(check_name(name));
    auto ends = TRY(pipe::create_duplex(nonblocking));
    intr::Guard guard;
    Listener* listener = Registry::find(name);
    ENSURE(listener, Error::NotFound);
    TRY(listener->enqueue(sys::move(ends.second)));
    return sys::move(ends.first);
}

Result<vfs::FileHandle> accept(Listener& listener, bool nonblocking) {
    auto stream = TRY(listener.take());
    TRY(stream->set_nonblocking(nonblocking));
    return stream;
}

}  // namespace channel
