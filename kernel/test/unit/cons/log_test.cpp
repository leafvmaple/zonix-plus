#include "test/test_defs.h"
#include "debug/backtrace.h"
#include "fs/vfs.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "mm/heap.h"
#include "sched/sched.h"
#include <kernel/log.h>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

constexpr size_t READ_BYTES = 4096;
// A function this small never spans more bytes than this.
constexpr uintptr_t FUNCTION_REACH = 256;

struct Trace {
    uintptr_t addresses[8]{};
    size_t count{};
};

// Each level does work after its call so that the call keeps its own frame.
[[gnu::noinline]] size_t deepest(Trace& trace) {
    trace.count = debug::collect_backtrace(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)), trace.addresses,
                                           sizeof(trace.addresses) / sizeof(trace.addresses[0]));
    return trace.count + 1;
}

[[gnu::noinline]] size_t middle(Trace& trace) {
    return deepest(trace) + 1;
}

[[gnu::noinline]] size_t outer(Trace& trace) {
    return middle(trace) + 1;
}

bool contains(const char* text, const char* wanted) {
    size_t length = strlen(wanted);
    for (; *text; ++text)
        if (str_starts_with(text, wanted))
            return true;
    return length == 0;
}

bool returns_into(uintptr_t address, size_t (*function)(Trace&)) {
    auto start = reinterpret_cast<uintptr_t>(function);
    return address > start && address < start + FUNCTION_REACH;
}

// A panic on real hardware is read from a backtrace, so each architecture's
// frame record layout must lead from callee to caller.
void test_backtrace() {
    TEST_START("Backtraces follow frame records from callee to caller");
    Trace trace;
    TEST_ASSERT(outer(trace) == trace.count + 3 && trace.count >= 3, "A three-level call yields at least three frames");
    TEST_ASSERT(returns_into(trace.addresses[0], middle) && returns_into(trace.addresses[1], outer),
                "The return addresses lie in the calling functions, nearest first");
    uintptr_t addresses[4];
    TEST_ASSERT(debug::collect_backtrace(0x10, addresses, 4) == 0 &&
                    debug::collect_backtrace(reinterpret_cast<uintptr_t>(&trace) + (1UL << 30), addresses, 4) == 0,
                "A frame pointer outside the kernel stack ends the walk at once");
    TEST_END();
}

// Reads everything available without waiting and reports whether `wanted` appeared.
bool read_until_empty(vfs::File& file, const char* wanted, bool& lost, bool& timestamped) {
    auto* buffer = static_cast<char*>(kmalloc(READ_BYTES + 1));
    if (!buffer)
        return false;
    bool found = false;
    for (;;) {
        auto count = file.read(buffer, READ_BYTES, 0);
        if (!count.ok() || count.value() <= 0)
            break;
        buffer[count.value()] = '\0';
        found = found || contains(buffer, wanted);
        lost = lost || contains(buffer, "records lost");
        // Lines start with "[seconds.millis] ".
        timestamped =
            timestamped || (buffer[0] == '[' && strchr(buffer, '.') != nullptr && strchr(buffer, ']') != nullptr);
    }
    kfree(buffer);
    return found;
}

// logd reads /dev/kmsg into the log file, and programs add their own records
// to it, so reading must follow the log without loss going unreported.
void test_kmsg() {
    TEST_START("/dev/kmsg reads the log as lines and takes records from programs");
    auto opened = vfs::open("/dev/kmsg", O_RDWR);
    TEST_ASSERT(opened.ok(), "Opened /dev/kmsg");
    if (!opened.ok()) {
        TEST_END();
        return;
    }
    vfs::FileHandle file = opened.release_value();
    TEST_ASSERT(file->set_nonblocking(true) == Error::None, "Reads can be made nonblocking");
    bool lost = false;
    bool timestamped = false;
    static_cast<void>(read_until_empty(*file, "", lost, timestamped));
    auto idle = file->read(nullptr, 0, 0);
    TEST_ASSERT(!idle.ok() && idle.error() == Error::Invalid, "A buffer shorter than a line is refused");
    char small[512];
    auto drained = file->read(small, sizeof(small), 0);
    TEST_ASSERT(!drained.ok() && drained.error() == Error::Busy, "A reader at the end does not wait when nonblocking");
    TEST_ASSERT(timestamped, "Lines carry a timestamp");

    const char record[] = "<4>kmsg test record\n";
    TEST_ASSERT(file->write(record, sizeof(record) - 1, 0).ok(), "A program writes a record");
    char wanted[96];
    Task* task = sched::current();
    snprintf(wanted, sizeof(wanted), "[warn] %s[%d]: kmsg test record\n", task->name(), task->pid);
    TEST_ASSERT(read_until_empty(*file, wanted, lost, timestamped),
                "The record is read back with its level, program and process");

    // Wrap the ring past this reader: it learns how much it missed.
    const char filler[] = "<7>kmsg filler\n";
    for (int i = 0; i < 600; ++i)
        static_cast<void>(file->write(filler, sizeof(filler) - 1, 0));
    lost = false;
    static_cast<void>(read_until_empty(*file, "", lost, timestamped));
    TEST_ASSERT(lost, "A reader overtaken by the ring is told that records were lost");

    // Shell redirection opens its target with O_CREAT and O_TRUNC.
    auto redirected = vfs::open("/dev/kmsg", O_WRONLY | O_CREAT | O_TRUNC);
    TEST_ASSERT(redirected.ok(), "A device opens with the flags of shell redirection");
    TEST_END();
}

}  // namespace

namespace log_test {

void test() {
    tests_passed = tests_failed = 0;
    test_backtrace();
    test_kmsg();
    TEST_SUMMARY("Kernel log");
}

}  // namespace log_test
