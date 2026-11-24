#include <kernel/log.h>
#include <kernel/boot_options.h>
#include "drivers/intr.h"
#include "fs/readiness.h"
#include "fs/vfs_fs.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/waitqueue.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <sys/array.hpp>

namespace {

constexpr size_t MESSAGE_SIZE = 256;
constexpr size_t RECORD_COUNT = 512;
// A formatted line: "[ seconds.millis] [level] message".
constexpr size_t LINE_SIZE = MESSAGE_SIZE + 32;

struct LogRecord {
    uint64_t sequence{};
    uint64_t time_ms{};
    klog::Level level{};
    bool from_user{};
    sys::array<char, MESSAGE_SIZE> message{};
};

int format(const LogRecord& record, char* out, size_t size) {
    return snprintf(out, size, "[%5lu.%03lu] [%s] %s", record.time_ms / 1000, record.time_ms % 1000,
                    klog::level_name(record.level), record.message.data());
}

void mark_truncated(LogRecord& record) {
    constexpr char suffix[] = "... [truncated]\n";
    constexpr size_t offset = MESSAGE_SIZE - sizeof(suffix);
    for (size_t i = 0; i < sizeof(suffix); ++i)
        record.message[offset + i] = suffix[i];
}

// Early boot cannot allocate. The log owns fixed record storage, while zstl
// supplies the array. IRQ guards serialize the current single-CPU kernel.
class KernelLog {
public:
    static void append(klog::Level level, const char* fmt, va_list args);
    static void store(LogRecord& record);
    static void dump();
    static void set_console_level(klog::Level level);
    static klog::Level console_level();

    // /dev/kmsg readers follow the log by sequence number.
    static uint64_t oldest();
    static uint64_t next();
    static bool copy(uint64_t sequence, LogRecord& out);
    static Error wait_after(uint64_t sequence, bool nonblocking);
    static void add_reader(int change);

private:
    inline static constinit sys::array<LogRecord, RECORD_COUNT> records_{};
    inline static constinit uint64_t next_sequence_{};
    inline static constinit klog::Level console_level_{BOOT_VERBOSE ? klog::Level::Debug : klog::Level::Info};
    inline static constinit size_t readers_{};
    inline static WaitQueue appended_{};
};

void KernelLog::append(klog::Level level, const char* fmt, va_list args) {
    LogRecord record{};
    record.level = level;
    int length = sys::vsnprintf(record.message.data(), record.message.size(), fmt, args);
    if (length >= static_cast<int>(record.message.size()))
        mark_truncated(record);
    store(record);
}

void KernelLog::store(LogRecord& record) {
    record.time_ms = trap::uptime_ms();
    char line[LINE_SIZE];
    intr::Guard guard;
    record.sequence = next_sequence_++;
    records_[record.sequence % RECORD_COUNT] = record;
    // Programs print their own output; their log records reach the console only as warnings.
    klog::Level threshold = record.from_user && console_level_ < klog::Level::Warn ? klog::Level::Warn : console_level_;
    if (record.level >= threshold) {
        format(record, line, sizeof(line));
        cprintf("%s", line);
    }
    if (readers_ != 0) {
        appended_.wakeup_all();
        fd::Readiness::notify();
    }
}

void KernelLog::dump() {
    uint64_t end;
    {
        intr::Guard guard;
        end = next_sequence_;
    }
    uint64_t begin = end > RECORD_COUNT ? end - RECORD_COUNT : 0;
    if (begin != 0) {
        cprintf("dmesg: %lu older records discarded\n", begin);
    }
    uint64_t overwritten{};
    char line[LINE_SIZE];
    for (uint64_t sequence = begin; sequence < end; ++sequence) {
        LogRecord record;
        if (!copy(sequence, record)) {
            ++overwritten;
            continue;
        }
        // Never hold interrupts off across the complete history dump. The local
        // copy remains valid even when a new message overwrites its ring slot.
        format(record, line, sizeof(line));
        cprintf("%s", line);
    }
    if (overwritten != 0) {
        cprintf("dmesg: %lu records overwritten while reading\n", overwritten);
    }
}

void KernelLog::set_console_level(klog::Level level) {
    intr::Guard guard;
    console_level_ = level;
}

klog::Level KernelLog::console_level() {
    intr::Guard guard;
    return console_level_;
}

uint64_t KernelLog::oldest() {
    intr::Guard guard;
    return next_sequence_ > RECORD_COUNT ? next_sequence_ - RECORD_COUNT : 0;
}

uint64_t KernelLog::next() {
    intr::Guard guard;
    return next_sequence_;
}

bool KernelLog::copy(uint64_t sequence, LogRecord& out) {
    intr::Guard guard;
    out = records_[sequence % RECORD_COUNT];
    return out.sequence == sequence && sequence < next_sequence_;
}

Error KernelLog::wait_after(uint64_t sequence, bool nonblocking) {
    intr::Guard guard;
    Task* task = sched::current();
    while (next_sequence_ <= sequence) {
        ENSURE(!nonblocking, Error::Busy);
        ENSURE(!task->termination_requested(), Error::Interrupted);
        appended_.sleep();
    }
    return Error::None;
}

void KernelLog::add_reader(int change) {
    intr::Guard guard;
    readers_ = static_cast<size_t>(static_cast<long>(readers_) + change);
}

// /dev/kmsg: each open file reads the log from the oldest kept record onward,
// in the console's line format, and a write adds one record from the writer.
class KmsgFile : public vfs::File {
public:
    KmsgFile() : cursor_(KernelLog::oldest()) { KernelLog::add_reader(1); }
    ~KmsgFile() override { KernelLog::add_reader(-1); }

    bool is_stream() const override { return true; }
    Result<bool> read_ready() override { return KernelLog::next() > cursor_; }
    Result<bool> write_ready() override { return true; }
    Error set_nonblocking(bool nonblocking) override {
        nonblocking_ = nonblocking;
        return Error::None;
    }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::CharDevice, 0, 0);
        return Error::None;
    }

    // Returns whole lines only; the buffer must hold the longest line.
    Result<int> read(void* buffer, size_t size, size_t) override {
        ENSURE(size >= LINE_SIZE, Error::Invalid);
        TRY(KernelLog::wait_after(cursor_, nonblocking_));
        auto* out = static_cast<char*>(buffer);
        size_t used = 0;
        char line[LINE_SIZE];
        for (;;) {
            uint64_t oldest = KernelLog::oldest();
            if (cursor_ < oldest) {
                int length = snprintf(line, sizeof(line), "kmsg: %lu records lost\n", oldest - cursor_);
                if (used + static_cast<size_t>(length) > size)
                    break;
                memcpy(out + used, line, static_cast<size_t>(length));
                used += static_cast<size_t>(length);
                cursor_ = oldest;
            }
            LogRecord record;
            if (cursor_ >= KernelLog::next())
                break;
            if (!KernelLog::copy(cursor_, record))
                continue;  // Overwritten meanwhile: the next pass reports the loss.
            int length = format(record, line, sizeof(line));
            size_t bytes = static_cast<size_t>(length) < sizeof(line) ? static_cast<size_t>(length) : sizeof(line) - 1;
            if (used + bytes > size)
                break;
            memcpy(out + used, line, bytes);
            used += bytes;
            ++cursor_;
        }
        return static_cast<int>(used);
    }

    // "<N>" in front selects the level: 0-3 error, 4 warning, 5-6 info, 7 debug.
    Result<int> write(const void* buffer, size_t size, size_t) override {
        const auto* text = static_cast<const char*>(buffer);
        size_t length = size;
        LogRecord record{};
        record.level = klog::Level::Info;
        record.from_user = true;
        if (length >= 3 && text[0] == '<' && text[1] >= '0' && text[1] <= '7' && text[2] == '>') {
            int priority = text[1] - '0';
            record.level = priority <= 3   ? klog::Level::Error
                           : priority == 4 ? klog::Level::Warn
                           : priority == 7 ? klog::Level::Debug
                                           : klog::Level::Info;
            text += 3;
            length -= 3;
        }
        while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
            --length;
        Task* task = sched::current();
        int prefix =
            snprintf(record.message.data(), MESSAGE_SIZE, "%s[%d]: ", task ? task->name() : "?", task ? task->pid : -1);
        size_t used = prefix > 0 && static_cast<size_t>(prefix) < MESSAGE_SIZE ? static_cast<size_t>(prefix) : 0;
        size_t room = MESSAGE_SIZE - used - 2;
        memcpy(record.message.data() + used, text, length < room ? length : room);
        used += length < room ? length : room;
        record.message[used] = '\n';
        record.message[used + 1] = '\0';
        if (length > room)
            mark_truncated(record);
        KernelLog::store(record);
        return static_cast<int>(size);
    }

private:
    uint64_t cursor_;
    bool nonblocking_{};
};

Result<vfs::FileHandle> open_kmsg() {
    vfs::FileHandle file(new (sys::nothrow) KmsgFile());
    ENSURE(file, Error::NoMem);
    return file;
}

}  // namespace

namespace klog {

void write(Level level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    KernelLog::append(level, fmt, args);
    va_end(args);
}

void dump() {
    KernelLog::dump();
}

int init_device() {
    return static_cast<int>(vfs::register_char_dev("kmsg", open_kmsg));
}

void set_console_level(Level level) {
    KernelLog::set_console_level(level);
}

Level console_level() {
    return KernelLog::console_level();
}

const char* level_name(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warn: return "warn";
        case Level::Error: return "error";
    }
    return "unknown";
}

}  // namespace klog
