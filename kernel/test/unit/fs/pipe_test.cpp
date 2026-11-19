#include "test/test_defs.h"
#include "fs/fd.h"
#include "fs/pipe.h"
#include "fs/readiness.h"
#include "lib/memory.h"
#include "mm/heap.h"
#include "sched/sched.h"

#include <sys/inplace_vector.hpp>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

constexpr size_t CAPACITY = 4096;

bool returned(Result<int> result, int expected) {
    return result.ok() && result.value() == expected;
}

bool failed(Result<int> result, Error expected) {
    return !result.ok() && result.error() == expected;
}

bool ready(Result<bool> result, bool expected) {
    return result.ok() && result.value() == expected;
}

void test_transfer() {
    TEST_START("Pipe delivers bytes in order and reports an empty pipe");
    auto created = pipe::create(true);
    TEST_ASSERT(created.ok(), "Created a nonblocking pipe");
    if (!created.ok()) {
        TEST_END();
        return;
    }
    auto ends = created.release_value();
    vfs::Stat st{};
    TEST_ASSERT(ends.read->stat(&st) == Error::None && st.type == vfs::NodeType::Pipe, "Ends report the pipe type");
    TEST_ASSERT(ready(ends.read->read_ready(), false) && ready(ends.write->write_ready(), true),
                "A new pipe is writable and has nothing to read");

    char out[8]{};
    TEST_ASSERT(failed(ends.read->read(out, sizeof(out), 0), Error::Busy), "Reading an empty pipe does not block");
    TEST_ASSERT(returned(ends.write->write("zonix", 5, 0), 5), "Wrote five bytes");
    TEST_ASSERT(ready(ends.read->read_ready(), true), "Written bytes make the pipe readable");
    TEST_ASSERT(returned(ends.read->read(out, 3, 0), 3) && memcmp(out, "zon", 3) == 0, "Short read takes the oldest bytes");
    TEST_ASSERT(returned(ends.read->read(out, sizeof(out), 0), 2) && memcmp(out, "ix", 2) == 0,
                "Next read returns only what is left");
    TEST_ASSERT(failed(ends.write->read(out, 1, 0), Error::Access) && failed(ends.read->write("x", 1, 0), Error::Access),
                "Each end refuses the other direction");
    TEST_END();
}

void test_capacity() {
    TEST_START("Pipe refuses writes beyond its capacity and wraps around");
    auto created = pipe::create(true);
    auto* block = static_cast<uint8_t*>(kmalloc(CAPACITY));
    TEST_ASSERT(created.ok() && block != nullptr, "Created a pipe and a full-size buffer");
    if (!created.ok() || !block) {
        kfree(block);
        TEST_END();
        return;
    }
    auto ends = created.release_value();
    for (size_t i = 0; i < CAPACITY; ++i) {
        block[i] = static_cast<uint8_t>(i * 7);
    }
    TEST_ASSERT(returned(ends.write->write(block, CAPACITY, 0), static_cast<int>(CAPACITY)), "Filled the pipe");
    TEST_ASSERT(ready(ends.write->write_ready(), false) && failed(ends.write->write(block, 1, 0), Error::Busy),
                "A full pipe is not writable");

    // Free space at the front, then write across the end of the buffer.
    uint8_t head[100]{};
    TEST_ASSERT(returned(ends.read->read(head, sizeof(head), 0), 100) && memcmp(head, block, 100) == 0,
                "Read the first hundred bytes");
    TEST_ASSERT(failed(ends.write->write(block, 101, 0), Error::Busy), "A write larger than the free space is refused");
    TEST_ASSERT(returned(ends.write->write(block, 100, 0), 100), "A write that fits the free space succeeds");

    bool ordered = true;
    for (size_t offset = 100; offset < CAPACITY + 100; ++offset) {
        uint8_t byte{};
        ordered = ordered && returned(ends.read->read(&byte, 1, 0), 1) && byte == block[offset % CAPACITY];
    }
    TEST_ASSERT(ordered, "Bytes written across the wrap come out in order");
    TEST_ASSERT(failed(ends.read->read(head, 1, 0), Error::Busy), "The pipe is empty again");
    kfree(block);
    TEST_END();
}

void test_closed_ends() {
    TEST_START("Pipe reports a closed peer");
    auto first = pipe::create(true);
    auto second = pipe::create(true);
    TEST_ASSERT(first.ok() && second.ok(), "Created two pipes");
    if (!first.ok() || !second.ok()) {
        TEST_END();
        return;
    }
    char out[4]{};
    auto drained = first.release_value();
    TEST_ASSERT(returned(drained.write->write("ab", 2, 0), 2), "Wrote before closing the writer");
    drained.write.reset();
    TEST_ASSERT(ready(drained.read->read_ready(), true), "A closed writer makes the pipe readable");
    TEST_ASSERT(returned(drained.read->read(out, sizeof(out), 0), 2) && memcmp(out, "ab", 2) == 0,
                "Buffered bytes survive the writer closing");
    TEST_ASSERT(returned(drained.read->read(out, sizeof(out), 0), 0), "Then the reader sees end of file");

    auto broken = second.release_value();
    broken.read.reset();
    TEST_ASSERT(ready(broken.write->write_ready(), true) && failed(broken.write->write("x", 1, 0), Error::Io),
                "Writing with no reader fails instead of blocking");
    TEST_END();
}

// Programs with many pipes, such as Desktop, need descriptors past the first
// sixteen, and their readiness must be reported at the documented bits.
void test_readiness_of_high_descriptors() {
    TEST_START("Readiness reports the read and write ends of a pipe on high descriptors");
    Task* task = sched::current();
    auto& files = task->files();
    sys::inplace_vector<int, fd::MAX_FD> opened;
    int ends[2]{-1, -1};
    bool created = true;
    while (created && ends[0] < 16) {
        created = files.create_pipe(ends, false, true) == Error::None;
        if (created) {
            assert(opened.try_push_back(ends[0]));
            assert(opened.try_push_back(ends[1]));
        }
    }
    TEST_ASSERT(created && ends[0] >= 16 && ends[1] < fd::MAX_FD, "A pipe was created on descriptors past sixteen");
    if (created) {
        const uint64_t reader = uint64_t{1} << ends[0];
        const uint64_t writer = uint64_t{1} << ends[1];
        auto ready = fd::Readiness::wait(task, reader, 0, writer);
        TEST_ASSERT(ready.ok() && ready.value() == writer << fd::MAX_FD,
                    "An empty pipe is ready for writing only, above the read set");
        TEST_ASSERT(files.close(ends[1]) == Error::None, "Closed the write end");
        opened.pop_back();
        ready = fd::Readiness::wait(task, reader, 0);
        TEST_ASSERT(ready.ok() && ready.value() == reader, "The read end is ready once the writer is gone");
    }
    auto outside = fd::Readiness::wait(task, uintptr_t{1} << fd::MAX_FD, 0);
    TEST_ASSERT(!outside.ok() && outside.error() == Error::Invalid, "A bit past the table is rejected");
    for (int fd : opened)
        static_cast<void>(files.close(fd));
    TEST_END();
}

}  // namespace

namespace pipe_test {

void test() {
    tests_passed = tests_failed = 0;
    test_transfer();
    test_capacity();
    test_closed_ends();
    test_readiness_of_high_descriptors();
    TEST_SUMMARY("Pipes");
}

}  // namespace pipe_test
