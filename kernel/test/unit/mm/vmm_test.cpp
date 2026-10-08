#include "test/test_defs.h"
#include "exec/exec.h"
#include "mm/vmm.h"
#include "mm/swap.h"
#include "lib/memory.h"
#include "drivers/intr.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include "lib/unistd.h"
#include "fs/vfs.h"
#include <asm/page.h>
#include <asm/pgtable.h>

static int tests_passed = 0;
static int tests_failed = 0;

static long syscall(int nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2) {
    TrapFrame tf{};
    tf.set_syscall(nr, arg0, arg1, arg2);
    if (!trap::handle_syscall(&tf)) {
        return -2;
    }
    return static_cast<long>(tf.syscall_return());
}

namespace {

class TransferFile : public vfs::File {
public:
    enum class Reply { Full, Short, Empty, FailAfterPage, Error, Negative, Oversized };

    void reset(Reply reply) {
        reply_ = reply;
        calls_ = 0;
        input_valid_ = true;
    }
    size_t calls() const { return calls_; }
    bool input_valid() const { return input_valid_; }

    Result<int> read(void* buf, size_t size, size_t offset) override {
        auto result = reply(size, offset);
        if (result.ok() && result.value() >= 0 && static_cast<size_t>(result.value()) <= size) {
            auto* bytes = static_cast<uint8_t*>(buf);
            for (int i = 0; i < result.value(); i++) {
                bytes[i] = static_cast<uint8_t>(offset + i);
            }
        }
        return result;
    }

    Result<int> write(const void* buf, size_t size, size_t offset) override {
        const auto* bytes = static_cast<const uint8_t*>(buf);
        for (size_t i = 0; i < size; i++) {
            input_valid_ = input_valid_ && bytes[i] == static_cast<uint8_t>(offset + i);
        }
        return reply(size, offset);
    }

    Error stat(vfs::Stat*) override { return Error::NotSupported; }

private:
    Reply reply_{Reply::Full};
    size_t calls_{};
    bool input_valid_{true};

    Result<int> reply(size_t size, size_t offset) {
        calls_++;
        switch (reply_) {
            case Reply::Short: return static_cast<int>(size / 2);
            case Reply::Empty: return 0;
            case Reply::FailAfterPage:
                if (offset >= PG_SIZE) {
                    return Error::Io;
                }
                break;
            case Reply::Error: return Error::Io;
            case Reply::Negative: return -1;
            case Reply::Oversized: return static_cast<int>(size + 1);
            case Reply::Full: break;
        }
        return static_cast<int>(size);
    }
};

}  // namespace

namespace vmm_test {

static void test_file_io(MemoryDesc& mm, uintptr_t base) {
    TEST_START("Syscall file transfers");
    Page* first = pmm::pgdir_alloc_page(mm.pgdir, base, user_page_perm(true));
    Page* second = pmm::pgdir_alloc_page(mm.pgdir, base + PG_SIZE, user_page_perm(true));
    TEST_ASSERT(first && second, "Mapped isolated syscall transfer buffers");
    if (!first || !second) {
        TEST_END();
        return;
    }
    auto* file = new (std::nothrow) TransferFile;
    TEST_ASSERT(file != nullptr, "Created syscall transfer file fixture");
    if (!file) {
        TEST_END();
        return;
    }
    Task* current = sched::current();
    auto fd_r = current->files().alloc(file);
    TEST_ASSERT(fd_r.ok(), "Allocated syscall transfer descriptor");
    if (!fd_r.ok()) {
        vfs::close(file);
        TEST_END();
        return;
    }
    const int fd = fd_r.value();
    fd::Entry* entry = current->files().get(fd);
    MemoryDesc* saved_mm = current->memory;
    current->memory = &mm;
    const uintptr_t user = base + PG_SIZE - 31;
    const size_t count = PG_SIZE + 17;

    TEST_ASSERT(syscall(NR_READ, fd, user, count) == static_cast<long>(count) &&
                    entry->offset == count && file->calls() == 2,
                "Read spans user pages and transfers more than one bounce-buffer chunk");
    const auto* first_bytes = static_cast<const uint8_t*>(pmm::page_to_kva(first));
    const auto* second_bytes = static_cast<const uint8_t*>(pmm::page_to_kva(second));
    bool contents_valid = true;
    for (size_t i = 0; i < count; i++) {
        size_t offset = PG_SIZE - 31 + i;
        uint8_t value = offset < PG_SIZE ? first_bytes[offset] : second_bytes[offset - PG_SIZE];
        contents_valid = contents_valid && value == static_cast<uint8_t>(i);
    }
    TEST_ASSERT(contents_valid, "Read copies complete data across the page and chunk boundaries");
    entry->offset = 0;
    file->reset(TransferFile::Reply::Full);
    TEST_ASSERT(syscall(NR_WRITE, fd, user, count) == static_cast<long>(count) &&
                    entry->offset == count && file->calls() == 2 && file->input_valid(),
                "Write copies complete user data before both VFS calls");

    constexpr int syscalls[] = {NR_READ, NR_WRITE};
    constexpr TransferFile::Reply failures[] = {
        TransferFile::Reply::Error, TransferFile::Reply::Negative, TransferFile::Reply::Oversized
    };
    for (int nr : syscalls) {
        entry->offset = 0;
        file->reset(TransferFile::Reply::Short);
        TEST_ASSERT(syscall(nr, fd, user, 128) == 64 && entry->offset == 64 && file->calls() == 1,
                    "Short file I/O stops and advances the offset by actual bytes");
        entry->offset = 0;
        file->reset(TransferFile::Reply::Empty);
        TEST_ASSERT(syscall(nr, fd, user, count) == 0 && entry->offset == 0 && file->calls() == 1,
                    "Zero-byte file I/O stops without advancing the offset");
        entry->offset = 0;
        file->reset(TransferFile::Reply::FailAfterPage);
        TEST_ASSERT(syscall(nr, fd, user, count) == PG_SIZE && entry->offset == PG_SIZE && file->calls() == 2,
                    "Failure after the first chunk preserves the completed byte count and offset");
        for (auto reply : failures) {
            entry->offset = 0;
            file->reset(reply);
            TEST_ASSERT(syscall(nr, fd, user, 128) == -1 && entry->offset == 0 && file->calls() == 1,
                        "Failed or invalid VFS byte counts do not advance the offset");
        }
        file->reset(TransferFile::Reply::Full);
        TEST_ASSERT(syscall(nr, fd, base + 2 * PG_SIZE - 1, 2) == -1 && file->calls() == 0,
                    "Entire user buffer is validated before the first VFS call");
        TEST_ASSERT(syscall(nr, fd::MAX_FD, 0, 0) == 0 && file->calls() == 0,
                    "Zero-length syscall preserves its existing no-I/O behavior");
    }
    TEST_ASSERT(syscall(NR_CLOSE, fd, 0, 0) == 0, "Closed syscall transfer fixture");
    current->memory = saved_mm;
    TEST_END();
}

void test() {
    tests_passed = tests_failed = 0;
    TEST_START("User memory permissions, copies and page faults");
    intr::Guard guard;
    TEST_ASSERT(vmm::Manager::kernel_pgdir() == __kernel_pg_dir &&
                    (reinterpret_cast<uintptr_t>(__kernel_pg_dir) & PG_MASK) == 0,
                "VM adopted the page-aligned assembly kernel root");
    MemoryDesc mm;
    mm.pgdir = exec::create_user_pgdir();
    TEST_ASSERT(mm.pgdir != nullptr, "Created isolated user address space");
    if (!mm.pgdir) {
        TEST_END();
        return;
    }
    constexpr uintptr_t base = 0x400000;
    Page* rw = pmm::pgdir_alloc_page(mm.pgdir, base, user_page_perm(true));
    Page* ro = pmm::pgdir_alloc_page(mm.pgdir, base + PG_SIZE, user_page_perm(false));
    TEST_ASSERT(rw && ro, "Mapped writable and read-only pages");
    if (!rw || !ro) {
        TEST_END();
        return;
    }
    memset(pmm::page_to_kva(rw), 0x5a, PG_SIZE);
    memset(pmm::page_to_kva(ro), 0xa5, PG_SIZE);
    const int owner_ref = rw->ref;
    {
        MemoryDesc borrowed{MemoryDesc::PageTableOwnership::Borrowed};
        borrowed.pgdir = mm.pgdir;
    }
    TEST_ASSERT(rw->ref == owner_ref && pmm::user_address(mm.pgdir, base, true).ok(),
                "Destroying a borrowed MM preserves the owner's mappings");
    uint8_t bytes[4]{};
    TEST_ASSERT(vmm::copy_from_user(&mm, bytes, base + PG_SIZE - 2, 4) == Error::None,
                "Read across writable/read-only page boundary succeeds");
    TEST_ASSERT(bytes[0] == 0x5a && bytes[1] == 0x5a && bytes[2] == 0xa5 && bytes[3] == 0xa5,
                "Cross-page copy preserves data");
    TEST_ASSERT(vmm::copy_to_user(&mm, base + PG_SIZE - 2, bytes, 4) == Error::Invalid,
                "Write crossing into read-only page rejected");
    TEST_ASSERT(!vmm::user_range_valid(&mm, base + 2 * PG_SIZE, 1, false), "Unmapped buffer rejected");
    TEST_ASSERT(!vmm::user_range_valid(&mm, USER_SPACE_TOP - 1, 2, false), "User-space overflow rejected");
    TEST_ASSERT(vmm::copy_from_user(&mm, bytes, 0, 1) == Error::Invalid, "Null user pointer rejected");

    Task* current = sched::current();
    MemoryDesc* saved_mm = current->memory;
    current->memory = &mm;
    TEST_ASSERT(syscall(NR_OPEN, base + 2 * PG_SIZE, 0, 0) == -1, "open rejects unmapped path");
    TEST_ASSERT(syscall(NR_READ, 0, base + PG_SIZE, 1) == -1, "read rejects read-only output buffer");
    TEST_ASSERT(syscall(NR_WRITE, 1, base + 2 * PG_SIZE, 1) == -1, "write rejects unmapped input buffer");
    current->memory = saved_mm;

    pte_t before = *pmm::get_pte(mm.pgdir, base + PG_SIZE, false);
    TEST_ASSERT(vmm::pg_fault(&mm, 2 | 4, base + PG_SIZE) == -1, "Write fault on present read-only page rejected");
    TEST_ASSERT(*pmm::get_pte(mm.pgdir, base + PG_SIZE, false) == before, "Permission fault leaves mapping intact");
    TEST_ASSERT(vmm::pg_fault(nullptr, 0, base) == -1, "Null address space rejected");
    TEST_ASSERT(vmm::pg_fault(&mm, 4, USER_SPACE_TOP) == -1, "Kernel-space fault rejected");
    TEST_ASSERT(vmm::pg_fault(&mm, 2 | 4, base + 2 * PG_SIZE) == 0, "Demand write fault resolved");
    bytes[0] = 0xff;
    TEST_ASSERT(vmm::copy_from_user(&mm, bytes, base + 2 * PG_SIZE, 1) == Error::None && bytes[0] == 0,
                "Demand page is zero-initialized");
    bytes[0] = 0x42;
    TEST_ASSERT(vmm::copy_to_user(&mm, base + 2 * PG_SIZE, bytes, 1) == Error::None, "Demand page is writable");
    pte_t* swapped = pmm::get_pte(mm.pgdir, base + 3 * PG_SIZE, true);
    TEST_ASSERT(swapped != nullptr, "Created swap-entry fixture");
    if (swapped) {
        // Offset 4 sets AArch64's AF bit but does not make a page valid.
        *swapped = (4U << 8) | swap::ENTRY_HAS_PERMISSIONS;
        TEST_ASSERT(!vmm::user_range_valid(&mm, base + 3 * PG_SIZE, 1, false), "Swap entry is not a present user page");
        TEST_ASSERT(vmm::pg_fault(&mm, 2 | 4, base + 3 * PG_SIZE) == -1,
                    "Write fault on swapped read-only page rejected before disk I/O");
    }
    TEST_END();
    test_file_io(mm, base + 4 * PG_SIZE);
    TEST_SUMMARY("User Memory");
}

}  // namespace vmm_test
