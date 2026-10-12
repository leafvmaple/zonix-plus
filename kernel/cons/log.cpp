#include <kernel/log.h>
#include <kernel/boot_options.h>
#include "drivers/intr.h"
#include "lib/stdio.h"
#include <sys/array.hpp>

namespace {

constexpr size_t MESSAGE_SIZE = 256;
constexpr size_t RECORD_COUNT = 128;

struct LogRecord {
    uint64_t sequence{};
    klog::Level level{};
    sys::array<char, MESSAGE_SIZE> message{};
};

// Early boot cannot allocate. The log owns fixed record storage, while zstl
// supplies the array. IRQ guards serialize the current single-CPU kernel.
class KernelLog {
public:
    static void append(klog::Level level, const char* fmt, va_list args);
    static void dump();
    static void set_console_level(klog::Level level);
    static klog::Level console_level();

private:
    inline static constinit sys::array<LogRecord, RECORD_COUNT> records_{};
    inline static constinit uint64_t next_sequence_{};
    inline static constinit klog::Level console_level_{BOOT_VERBOSE ? klog::Level::Debug : klog::Level::Info};
};

void KernelLog::append(klog::Level level, const char* fmt, va_list args) {
    LogRecord record{};
    record.level = level;
    int length = sys::vsnprintf(record.message.data(), record.message.size(), fmt, args);
    if (length >= static_cast<int>(record.message.size())) {
        constexpr char suffix[] = "... [truncated]\n";
        constexpr size_t offset = MESSAGE_SIZE - sizeof(suffix);
        for (size_t i = 0; i < sizeof(suffix); ++i) {
            record.message[offset + i] = suffix[i];
        }
    }

    intr::Guard guard;
    record.sequence = next_sequence_++;
    records_[record.sequence % RECORD_COUNT] = record;
    if (level >= console_level_) {
        cprintf("[%s] %s", klog::level_name(level), record.message.data());
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
    for (uint64_t sequence = begin; sequence < end; ++sequence) {
        LogRecord record;
        {
            intr::Guard guard;
            record = records_[sequence % RECORD_COUNT];
        }
        if (record.sequence != sequence) {
            ++overwritten;
            continue;
        }
        // Never hold interrupts off across the complete history dump. The local
        // copy remains valid even when a new message overwrites its ring slot.
        cprintf("[%s] %s", klog::level_name(record.level), record.message.data());
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
