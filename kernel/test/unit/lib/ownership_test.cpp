#include "test/test_defs.h"
#include "lib/kernel_buffer.h"
#include "fs/fd.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

struct Counts {
    int destroyed{};
};

class Object {
public:
    explicit Object(Counts& counts) : counts_(counts) {}
    ~Object() { ++counts_.destroyed; }
    int value{42};

private:
    Counts& counts_;
};

class CountingFile : public vfs::File {
public:
    explicit CountingFile(Counts& counts) : counts_(counts) {}
    ~CountingFile() override { ++counts_.destroyed; }
    Result<int> read(void*, size_t, size_t) override { return 0; }
    Result<int> write(const void*, size_t, size_t) override { return Error::NotSupported; }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::File, 0, 0);
        return Error::None;
    }

private:
    Counts& counts_;
};

static_assert(sizeof(sys::unique_ptr<Object>) == sizeof(Object*));
static_assert(sizeof(vfs::FileHandle) == sizeof(vfs::File*));
static_assert(!__is_constructible(sys::unique_ptr<Object>, const sys::unique_ptr<Object>&));
static_assert(__is_nothrow_constructible(sys::unique_ptr<Object>, sys::unique_ptr<Object>&&));
static_assert(!__is_constructible(KernelBuffer, const KernelBuffer&));
static_assert(__is_nothrow_constructible(KernelBuffer, KernelBuffer&&));
static_assert(!__is_constructible(fd::Table, const fd::Table&));
static_assert(!__is_constructible(fd::Table, fd::Table&&));
static_assert(__is_same(decltype(static_cast<const KernelBuffer*>(nullptr)->data()), const uint8_t*));

Result<sys::unique_ptr<Object>> make_object(Counts& counts) {
    auto owner = sys::unique_ptr<Object>(new (sys::nothrow) Object(counts));
    ENSURE(owner, Error::NoMem);
    return owner;
}

Error reject_object(Counts& counts) {
    auto owner = TRY(make_object(counts));
    return Error::Io;
}

Result<vfs::FileHandle> make_file(Counts& counts) {
    auto owner = vfs::FileHandle(new (sys::nothrow) CountingFile(counts));
    ENSURE(owner, Error::NoMem);
    return owner;
}

void test_single_owner() {
    TEST_START("Unique ownership moves, borrows and hands off exactly once");
    Counts counts;
    auto first = make_object(counts);
    auto second = make_object(counts);
    TEST_ASSERT(first.ok() && second.ok(), "Allocated both owned objects");
    if (!first.ok() || !second.ok()) {
        TEST_END();
        return;
    }
    {
        auto owner = first.release_value();
        Object* borrowed = owner.get();
        auto moved = sys::move(owner);
        TEST_ASSERT(!owner && moved.get() == borrowed && moved->value == 42,
                    "Move empties source and keeps the borrowed object alive");
        auto& same = moved;
        moved = sys::move(same);
        TEST_ASSERT(moved.get() == borrowed && counts.destroyed == 0, "Self move preserves ownership");
        auto replacement = second.release_value();
        replacement = sys::move(moved);
        TEST_ASSERT(!moved && replacement.get() == borrowed && counts.destroyed == 1,
                    "Move assignment destroys the replaced object once");
        Object* transferred = replacement.release();
        TEST_ASSERT(!replacement && counts.destroyed == 1, "Release hands off without destroying");
        auto receiver = sys::unique_ptr<Object>(transferred);
        receiver.reset();
        receiver.reset();
        TEST_ASSERT(!receiver && counts.destroyed == 2, "Reset destroys once and is safe when empty");
    }
    TEST_ASSERT(counts.destroyed == 2, "Moved-from owners do not destroy transferred objects");
    TEST_ASSERT(reject_object(counts) == Error::Io && counts.destroyed == 3,
                "Early error propagation destroys the local owner");
    TEST_END();
}

void test_buffer_owner() {
    TEST_START("KernelBuffer moves bytes and length as one resource");
    auto allocation = KernelBuffer::alloc(32);
    auto replacement = KernelBuffer::alloc(16);
    TEST_ASSERT(allocation.ok() && replacement.ok(), "Allocated byte buffers");
    if (!allocation.ok() || !replacement.ok()) {
        TEST_END();
        return;
    }
    auto buffer = allocation.release_value();
    buffer.data()[31] = 0x5a;
    uint8_t* borrowed = buffer.data();
    auto moved = sys::move(buffer);
    TEST_ASSERT(buffer.empty() && buffer.data() == nullptr && moved.size() == 32 && moved.data() == borrowed,
                "Move preserves length/data and empties both source properties");
    auto& same = moved;
    moved = sys::move(same);
    TEST_ASSERT(moved.size() == 32 && moved.data()[31] == 0x5a, "Self move preserves buffer contents");
    auto target = replacement.release_value();
    target = sys::move(moved);
    const auto& view = target;
    TEST_ASSERT(moved.empty() && moved.data() == nullptr && view.size() == 32 && view.data()[31] == 0x5a,
                "Move assignment transfers the complete buffer and const access borrows read-only bytes");
    auto empty = KernelBuffer::alloc(0);
    TEST_ASSERT(empty.ok() && empty.value().empty() && empty.value().data() == nullptr,
                "Zero-byte allocation succeeds as an empty buffer");
    sys::unique_ptr<uint8_t[]> zero_array(new (sys::nothrow) uint8_t[0]);
    sys::unique_ptr<uint8_t[]> ordinary_array(new uint8_t[0]);
    TEST_ASSERT(zero_array && ordinary_array, "Zero-element C++ arrays still receive valid owned allocations");
    TEST_END();
}

void test_descriptor_ownership() {
    TEST_START("Descriptor table owns handles and closes rejected transfers");
    Counts counts;
    {
        fd::Table table;
        for (int i = 0; i < fd::MAX_FD; ++i) {
            auto owner = make_file(counts);
            TEST_ASSERT(owner.ok(), "Allocated descriptor file");
            if (!owner.ok()) {
                TEST_END();
                return;
            }
            auto fd = table.alloc(owner.release_value());
            TEST_ASSERT(fd.ok() && fd.value() == i, "Descriptor takes ownership in the next slot");
        }
        auto extra = make_file(counts);
        TEST_ASSERT(extra.ok(), "Allocated rejected file");
        if (!extra.ok()) {
            TEST_END();
            return;
        }
        auto full = table.alloc(extra.release_value());
        TEST_ASSERT(!full.ok() && full.error() == Error::Full && counts.destroyed == 1,
                    "Full table closes incoming owner without affecting existing files");
        TEST_ASSERT(table.close(0) == Error::None && counts.destroyed == 2, "Close destroys the slot owner once");
        TEST_ASSERT(table.close(0) == Error::Invalid && counts.destroyed == 2, "Repeated close does not destroy twice");
        table.close_all();
        table.close_all();
        TEST_ASSERT(counts.destroyed == fd::MAX_FD + 1, "Repeated close_all is safe");
        TEST_ASSERT(!table.alloc(vfs::FileHandle{}).ok(), "Empty ownership cannot become a descriptor");
    }
    TEST_ASSERT(counts.destroyed == fd::MAX_FD + 1, "Empty table destruction does not close again");
    TEST_END();
}

void test_table_destruction_and_fork() {
    TEST_START("Table teardown closes files while unsupported sharing preserves both owners");
    Counts counts;
    {
        fd::Table parent;
        fd::Table child;
        auto first = make_file(counts);
        auto second = make_file(counts);
        TEST_ASSERT(first.ok() && second.ok(), "Allocated parent and child files");
        if (!first.ok() || !second.ok()) {
            TEST_END();
            return;
        }
        TEST_ASSERT(parent.alloc(first.release_value()).ok() && child.alloc(second.release_value()).ok(),
                    "Each table owns a distinct handle");
        TEST_ASSERT(child.fork_from(parent, fd::ForkPolicy::Share) == Error::NotSupported && parent.get(0) &&
                        child.get(0) && counts.destroyed == 0,
                    "Sharing rejection neither duplicates nor destroys ownership");
        TEST_ASSERT(child.fork_from(parent, fd::ForkPolicy::Reset) == Error::None && !child.get(0) && parent.get(0) &&
                        counts.destroyed == 1,
                    "Reset closes the child's old handle and preserves parent ownership");
    }
    TEST_ASSERT(counts.destroyed == 2, "Scope exit closes the remaining parent handle");
    TEST_END();
}

}  // namespace

namespace ownership_test {
void test() {
    tests_passed = tests_failed = 0;
    test_single_owner();
    test_buffer_owner();
    test_descriptor_ownership();
    test_table_destruction_and_fork();
    TEST_SUMMARY("Resource Ownership");
}
}  // namespace ownership_test
