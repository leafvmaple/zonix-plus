#include "pipe.h"
#include "fs/fd.h"
#include "drivers/intr.h"
#include "lib/waitqueue.h"
#include "sched/sched.h"
#include <sys/array.hpp>

namespace pipe {
namespace {
class Buffer;
struct ReleaseBuffer {
    void operator()(Buffer* buffer) const noexcept;
};
using BufferRef = sys::unique_ptr<Buffer, ReleaseBuffer>;

// References belong to endpoint Files, not individual duplicated descriptors.
class Buffer {
public:
    BufferRef share() {
        ++references_;
        return BufferRef(this);
    }
    void open(bool writer) { (writer ? writer_open_ : reader_open_) = true; }
    void close(bool writer) {
        intr::Guard guard;
        (writer ? writer_open_ : reader_open_) = false;
        readable_.wakeup_all();
        writable_.wakeup_all();
    }
    Result<int> read(void* out, size_t size) {
        while (true) {
            intr::Guard guard;
            ENSURE(!sched::current()->termination_requested(), Error::Interrupted);
            if (count_ != 0) {
                size_t count = size < count_ ? size : count_;
                auto* bytes = static_cast<uint8_t*>(out);
                for (size_t i = 0; i < count; ++i) {
                    bytes[i] = bytes_[read_];
                    read_ = (read_ + 1) % bytes_.size();
                }
                count_ -= count;
                writable_.wakeup_all();
                return static_cast<int>(count);
            }
            if (!writer_open_)
                return 0;
            readable_.sleep();
        }
    }
    Result<int> write(const void* in, size_t size) {
        while (true) {
            intr::Guard guard;
            ENSURE(!sched::current()->termination_requested(), Error::Interrupted);
            ENSURE(reader_open_, Error::Io);  // No SIGPIPE ABI: report a failed write.
            if (count_ < bytes_.size()) {
                size_t count = size < bytes_.size() - count_ ? size : bytes_.size() - count_;
                const auto* bytes = static_cast<const uint8_t*>(in);
                for (size_t i = 0; i < count; ++i)
                    bytes_[(read_ + count_ + i) % bytes_.size()] = bytes[i];
                count_ += count;
                readable_.wakeup_all();
                return static_cast<int>(count);
            }
            writable_.sleep();
        }
    }

private:
    friend struct ReleaseBuffer;
    size_t references_{1};
    sys::array<uint8_t, 4096> bytes_{};
    size_t read_{};
    size_t count_{};
    bool reader_open_{};
    bool writer_open_{};
    WaitQueue readable_;
    WaitQueue writable_;
};

void ReleaseBuffer::operator()(Buffer* buffer) const noexcept {
    intr::Guard guard;
    if (--buffer->references_ == 0)
        delete buffer;
}

class Endpoint : public vfs::File {
public:
    Endpoint(BufferRef buffer, bool writer)
        : File(writer ? O_WRONLY : O_RDONLY), buffer_(sys::move(buffer)), writer_(writer) {
        buffer_->open(writer_);
    }
    ~Endpoint() override { buffer_->close(writer_); }
    bool is_stream() const override { return true; }
    Result<int> read(void* out, size_t size, size_t) override {
        return writer_ ? Result<int>(Error::Access) : buffer_->read(out, size);
    }
    Result<int> write(const void* in, size_t size, size_t) override {
        return writer_ ? buffer_->write(in, size) : Result<int>(Error::Access);
    }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::Pipe, 0, 0);
        return Error::None;
    }

private:
    BufferRef buffer_;
    bool writer_;
};
}  // namespace

Result<Ends> create() {
    BufferRef buffer(new (sys::nothrow) Buffer());
    ENSURE(buffer, Error::NoMem);
    Ends ends;
    ends.read.reset(new (sys::nothrow) Endpoint(buffer->share(), false));
    ENSURE(ends.read, Error::NoMem);
    ends.write.reset(new (sys::nothrow) Endpoint(buffer->share(), true));
    ENSURE(ends.write, Error::NoMem);
    return ends;
}
}  // namespace pipe

namespace fd {
Error Table::create_pipe(int (&ends)[2], bool close_on_exec) {
    ENSURE(size() <= MAX_FD - 2, Error::Full);
    auto files = TRY(pipe::create());
    int reader = TRY(alloc(sys::move(files.read), close_on_exec));
    auto writer = alloc(sys::move(files.write), close_on_exec);
    if (!writer.ok()) {
        static_cast<void>(close(reader));
        return writer.error();
    }
    ends[0] = reader;
    ends[1] = writer.value();
    return Error::None;
}

}  // namespace fd
