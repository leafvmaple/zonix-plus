#include "test/test_defs.h"
#include "test/task_access.h"
#include "test/unit/fs/fat_image.h"
#include "fs/vfs.h"
#include "exec/exec.h"
#include "lib/kernel_buffer.h"
#include "lib/semaphore.h"
#include "mm/page_table.h"
#include "drivers/intr.h"
#include "trap/trap.h"
#include <kernel/power.h>
#include <abi/syscall.h>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

class CachedDisk : public fat_test::FatImage {
public:
    CachedDisk() : FatImage(1, 32, 2) {
        if (!FatImage::ready())
            return;
        clear_files();
        auto memory = KernelBuffer::alloc(block_count * BLOCK_SIZE_BYTES);
        if (!memory.ok())
            return;
        durable_ = memory.release_value();
        assert(FatImage::read_blocks(0, durable_.data(), block_count) == Error::None);
    }
    bool ready() const { return FatImage::ready() && !durable_.empty(); }
    void fail_flush(Error error) { flush_error_ = error; }
    size_t flush_calls() const { return flush_calls_; }
    bool is_durable() const { return memcmp(durable_.data(), &boot_sector(), durable_.size()) == 0; }
    FatDirEntry durable_entry(const FatInfo::EntryLocation& location) const {
        const auto* entries =
            reinterpret_cast<const FatDirEntry*>(durable_.data() + location.sector * BLOCK_SIZE_BYTES);
        return entries[location.index];
    }
    void hold_next_write(Semaphore& ready, Semaphore& release) {
        write_ready_ = &ready;
        write_release_ = &release;
        held_ = false;
    }
    bool has_held_write() const { return held_; }
    void cancel_hold() { write_ready_ = write_release_ = nullptr; }

private:
    Error write_blocks(uint32_t start_lba, const void* bytes, size_t count) override {
        TRY(FatImage::write_blocks(start_lba, bytes, count));
        if (write_ready_) {
            Semaphore* ready = write_ready_;
            Semaphore* release = write_release_;
            cancel_hold();
            held_ = true;
            ready->up();
            release->down();
        }
        return Error::None;
    }
    Error flush_cache() override {
        ++flush_calls_;
        if (flush_error_ != Error::None)
            return flush_error_;
        return FatImage::read_blocks(0, durable_.data(), block_count);
    }
    KernelBuffer durable_;
    Error flush_error_{Error::None};
    size_t flush_calls_{};
    Semaphore* write_ready_{};
    Semaphore* write_release_{};
    bool held_{};
};

class SyncVolume {
public:
    SyncVolume() { mounted_ = disk.ready() && vfs::mount("/mnt", &disk, "fat") == Error::None; }
    ~SyncVolume() {
        if (mounted_)
            assert(vfs::umount("/mnt") == Error::None);
    }
    bool ready() const { return mounted_; }
    CachedDisk disk;

private:
    bool mounted_{};
};

vfs::FileHandle open_file(const char* path, int flags) {
    auto result = vfs::open(path, flags);
    return result.ok() ? result.release_value() : vfs::FileHandle{};
}

bool wrote(vfs::File* file, const void* bytes, size_t count, size_t offset = 0) {
    auto result = vfs::write(file, bytes, count, offset);
    return result.ok() && result.value() == static_cast<int>(count);
}

void test_sync_persistence() {
    TEST_START("P0-2: sync commits data, directory entries and FAT copies to device cache");
    CachedDisk disk;
    FatInfo fs;
    TEST_ASSERT(disk.ready() && fs.mount(&disk) == Error::None,
                "Mounted disk with separate volatile and durable images");
    if (!disk.ready()) {
        TEST_END();
        return;
    }
    TEST_ASSERT(fs.mkdir("DIR") == Error::None && fs.create_file("DIR/STATE.BIN") == Error::None,
                "Created directory and file without syncing");
    FatDirEntry entry{};
    FatInfo::EntryLocation location{};
    TEST_ASSERT(fs.find_file("DIR/STATE.BIN", &entry, &location) == Error::None, "Located nested file metadata");
    const char bytes[] = "durable payload";
    auto result = fs.write_file(location, reinterpret_cast<const uint8_t*>(bytes), 0, sizeof(bytes));
    TEST_ASSERT(result.ok() && result.value() == sizeof(bytes) && fs.needs_sync() && !disk.is_durable(),
                "Completed writes remain uncommitted in the emulated device cache");
    TEST_ASSERT(fs.sync() == Error::None && !fs.needs_sync() && disk.is_durable() && disk.flush_calls() == 1,
                "Sync returns success only after the complete device image is durable");
    const FatDirEntry committed = disk.durable_entry(location);
    TEST_ASSERT(committed.file_size == sizeof(bytes) && committed.cluster() >= 2,
                "Durable metadata contains the final size and allocated chain");
    TEST_ASSERT(fs.unmount() == Error::None, "Clean unmount also completes checked synchronization");
    TEST_END();
}

void test_flush_failure_retry() {
    TEST_START("P0-2: flush failure preserves pending state and permits sync/unmount retries");
    CachedDisk disk;
    FatInfo fs;
    TEST_ASSERT(disk.ready() && fs.mount(&disk) == Error::None, "Mounted flush failure disk");
    if (!disk.ready()) {
        TEST_END();
        return;
    }
    TEST_ASSERT(fs.create_file("STATE.BIN") == Error::None, "Created flush failure target");
    FatDirEntry entry{};
    FatInfo::EntryLocation location{};
    TEST_ASSERT(fs.find_file("STATE.BIN", &entry, &location) == Error::None, "Found target directory slot");
    uint8_t byte = 0xA5;
    auto result = fs.write_file(location, &byte, 0, 1);
    TEST_ASSERT(result.ok() && result.value() == 1, "Wrote target through volatile cache");
    disk.fail_flush(Error::Timeout);
    TEST_ASSERT(fs.sync() == Error::Timeout && fs.needs_sync() && !disk.is_durable(),
                "Flush timeout does not clear pending durability or update durable image");
    TEST_ASSERT(fs.unmount() == Error::Timeout && fs.needs_sync() && fs.find_file("STATE.BIN", &entry) == Error::None &&
                    entry.file_size == 1,
                "Failed unmount retains the usable filesystem and its pending state");
    disk.fail_flush(Error::None);
    TEST_ASSERT(fs.sync() == Error::None && !fs.needs_sync() && disk.is_durable(),
                "Retry commits previously completed data and metadata");
    TEST_ASSERT(fs.unmount() == Error::None, "Unmount succeeds after the device recovers");
    TEST_END();
}

void test_metadata_sync_failure() {
    TEST_START("P0-2: FAT mirror errors precede cache flush and preserve retry state");
    CachedDisk disk;
    FatInfo fs;
    TEST_ASSERT(disk.ready() && fs.mount(&disk) == Error::None, "Mounted mirrored-cache failure disk");
    if (!disk.ready()) {
        TEST_END();
        return;
    }
    TEST_ASSERT(fs.create_file("STATE.BIN") == Error::None, "Created mirror failure target");
    FatDirEntry entry{};
    FatInfo::EntryLocation location{};
    TEST_ASSERT(fs.find_file("STATE.BIN", &entry, &location) == Error::None, "Located mirror target");
    disk.fail_writes_at(disk.fat_lba(3, 1), Error::NoDevice);
    const uint8_t byte = 7;
    auto result = fs.write_file(location, &byte, 0, 1);
    TEST_ASSERT(!result.ok() && result.error() == Error::NoDevice, "Injected incomplete mirrored FAT write");
    TEST_ASSERT(fs.sync() == Error::NoDevice && fs.needs_sync() && disk.flush_calls() == 0 && !disk.is_durable(),
                "Failed metadata retry cannot claim a device durability barrier");
    disk.fail_writes_at(0, Error::None);
    TEST_ASSERT(fs.sync() == Error::None && disk.flush_calls() == 1 && disk.is_durable() && !fs.needs_sync() &&
                    disk.link(3, 0) == disk.link(3, 1),
                "Successful retry writes both FAT copies before flushing cache");
    TEST_ASSERT(fs.unmount() == Error::None, "Recovered mirror state can be unmounted");
    TEST_END();
}

class UnknownCacheDisk : public BlockDevice {
    Error read_blocks(uint32_t, void*, size_t) override { return Error::None; }
    Error write_blocks(uint32_t, const void*, size_t) override { return Error::None; }
};

void test_flush_capability() {
    TEST_START("P0-2: unknown flush capability never silently reports persistence");
    UnknownCacheDisk disk;
    TEST_ASSERT(disk.flush() == Error::NotSupported, "An unimplemented device flush is explicitly unsupported");
    CachedDisk cached;
    FatInfo fs;
    TEST_ASSERT(cached.ready() && fs.mount(&cached) == Error::None, "Mounted unsupported flush fixture");
    if (!cached.ready()) {
        TEST_END();
        return;
    }
    TEST_ASSERT(fs.create_file("STATE.BIN") == Error::None, "Created metadata needing persistence");
    cached.fail_flush(Error::NotSupported);
    TEST_ASSERT(fs.sync() == Error::NotSupported && fs.needs_sync(), "Unsupported durability is propagated to caller");
    cached.fail_flush(Error::None);
    TEST_ASSERT(fs.unmount() == Error::None, "Explicit recovery permits cleanup");
    TEST_END();
}

void test_failed_shutdown() {
    TEST_START("P0-2: failed shutdown never powers off and restores filesystem/block access");
    SyncVolume volume;
    TEST_ASSERT(volume.ready(), "Mounted shutdown failure volume");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto file = open_file("/mnt/STATE.BIN", O_CREAT | O_RDWR);
    TEST_ASSERT(file && wrote(file.get(), "old", 3), "Created data before shutdown attempt");
    volume.disk.fail_flush(Error::Io);
    TEST_ASSERT(power::shutdown() == Error::Io, "Actual shutdown API returns the sync error before platform power-off");
    TEST_ASSERT(!vfs::is_frozen() && !BlockManager::is_frozen() && wrote(file.get(), "new", 3),
                "Both admission gates reopen and existing file remains writable");
    auto memory = KernelBuffer::alloc(512);
    TEST_ASSERT(memory.ok(), "Allocated recovery read buffer");
    if (memory.ok()) {
        auto bytes = memory.release_value();
        TEST_ASSERT(volume.disk.read(0, bytes.data(), 1) == Error::None, "Raw block I/O is admitted after failure");
    }
    volume.disk.fail_flush(Error::None);
    TEST_ASSERT(vfs::sync() == Error::None && volume.disk.is_durable(),
                "Standalone sync commits the recovered state without requiring handle closure");
    TEST_END();
}

void test_prepared_shutdown() {
    TEST_START("P0-2: prepared shutdown holds storage frozen while console remains usable");
    SyncVolume volume;
    TEST_ASSERT(volume.ready(), "Mounted prepared-shutdown volume");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto file = open_file("/mnt/STATE.BIN", O_CREAT | O_RDWR);
    TEST_ASSERT(file && wrote(file.get(), "ready", 5), "Created data before quiesce");
    auto memory = KernelBuffer::alloc(512);
    TEST_ASSERT(memory.ok(), "Allocated frozen-device probe buffer");
    if (!memory.ok()) {
        TEST_END();
        return;
    }
    auto bytes = memory.release_value();
    {
        auto prepared = power::prepare_shutdown();
        TEST_ASSERT(prepared.ok() && vfs::is_frozen() && BlockManager::is_frozen() && volume.disk.is_durable(),
                    "Preparation drains both layers and commits the device cache");
        if (prepared.ok()) {
            TEST_ASSERT(vfs::create("/mnt/LATE.TXT") == Error::Busy &&
                            volume.disk.write(0, bytes.data(), 1) == Error::Busy,
                        "No new file mutation or raw write can enter after final synchronization");
            auto again = power::prepare_shutdown();
            TEST_ASSERT(!again.ok() && again.error() == Error::Busy && vfs::is_frozen(),
                        "Competing preparation cannot release another shutdown lease");
            auto console = open_file("/dev/console", O_WRONLY);
            TEST_ASSERT(console && wrote(console.get(), "\n", 1),
                        "Virtual console stays usable while storage is frozen");
        }
    }
    TEST_ASSERT(!vfs::is_frozen() && !BlockManager::is_frozen() && vfs::create("/mnt/LATE.TXT") == Error::None,
                "Cancelling the lease restores normal filesystem operation");
    TEST_END();
}

constexpr uintptr_t USER_BASE = 0x500000;
constexpr size_t WRITE_SIZE = PG_SIZE + 37;

long invoke(int nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2) {
    sys::unique_ptr<TrapFrame> tf(new (sys::nothrow) TrapFrame{});
    if (!tf)
        return -2;
    tf->set_syscall(nr, arg0, arg1, arg2);
    if (!trap::handle_syscall(tf.get()))
        return -2;
    return static_cast<long>(tf->syscall_return());
}

struct DrainState {
    CachedDisk* disk;
    MemoryDesc* memory;
    const uint8_t* raw_bytes{};
    Semaphore started{};
    Semaphore allow_write{};
    Semaphore prepared{};
    Semaphore release_shutdown{};
    long written{-1};
    Error error{Error::Fail};
};

void test_reentrant_shutdown() {
    TEST_START("P0-2: shutdown from an admitted operation fails without self-deadlocking");
    SyncVolume volume;
    TEST_ASSERT(volume.ready(), "Mounted reentrant shutdown fixture");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto file = open_file("/mnt/STATE.BIN", O_CREAT | O_RDWR);
    TEST_ASSERT(static_cast<bool>(file), "Created reentrant operation target");
    if (!file) {
        TEST_END();
        return;
    }
    {
        ActivityGuard activity;
        TEST_ASSERT(file->begin_io(activity) == Error::None, "Admitted outer filesystem operation");
        auto prepared = power::prepare_shutdown();
        TEST_ASSERT(!prepared.ok() && prepared.error() == Error::Busy && !vfs::is_frozen() &&
                        !BlockManager::is_frozen(),
                    "Reentrant preparation returns Busy and leaves both gates open");
    }
    TEST_ASSERT(wrote(file.get(), "ok", 2), "Outer admission can finish and release normally");
    TEST_END();
}

int write_worker(void* raw) {
    auto& state = *static_cast<DrainState*>(raw);
    if (state.memory) {
        intr::Guard guard;
        const uintptr_t root = arch_read_page_table_root();
        ScopedTaskMemory view(*sched::current(), *state.memory);
        const int fd = static_cast<int>(invoke(NR_OPEN, USER_BASE, O_WRONLY, 0));
        if (fd >= 0) {
            state.written = invoke(NR_WRITE, fd, USER_BASE + PG_SIZE, WRITE_SIZE);
            if (invoke(NR_CLOSE, fd, 0, 0) != 0)
                state.written = -1;
        }
        arch_load_page_table_root(root);
    } else {
        Error error = state.disk->write(state.disk->cluster_lba(3), state.raw_bytes, 1);
        state.written = error == Error::None ? 512 : -1;
    }
    if (!state.disk->has_held_write())
        state.started.up();
    return state.written > 0 ? 0 : 1;
}

int shutdown_worker(void* raw) {
    auto& state = *static_cast<DrainState*>(raw);
    auto prepared = power::prepare_shutdown();
    state.error = prepared.ok() ? Error::None : prepared.error();
    state.prepared.up();
    if (prepared.ok())
        state.release_shutdown.down();
    return prepared.ok() ? 0 : 1;
}

void wait_child(int pid, int* code) {
    auto result = sched::wait(pid, code);
    assert(result.ok() && result.value() == pid);
}

void test_drain(bool syscall) {
    TEST_START(syscall ? "P0-2: freeze waits for a complete multi-chunk syscall including nested requests"
                       : "P0-2: quiesce independently drains raw block requests");
    SyncVolume volume;
    MemoryDesc memory;
    auto allocation = KernelBuffer::alloc(WRITE_SIZE);
    TEST_ASSERT(volume.ready() && allocation.ok(), "Prepared drain fixture and payload");
    if (!volume.ready() || !allocation.ok()) {
        TEST_END();
        return;
    }
    auto bytes = allocation.release_value();
    memset(bytes.data(), 0xA5, bytes.size());
    auto file = open_file("/mnt/STATE.BIN", O_CREAT | O_RDWR);
    TEST_ASSERT(static_cast<bool>(file), "Created target before freezing admission");
    if (!file) {
        TEST_END();
        return;
    }
    if (syscall) {
        auto root = exec::create_user_pgdir();
        TEST_ASSERT(root.ok(), "Created isolated user address space");
        if (!root.ok()) {
            TEST_END();
            return;
        }
        memory.pgdir = root.release_value();
        bool mapped = true;
        for (size_t i = 0; i < 3; ++i) {
            mapped = pmm::alloc_and_map_page(memory.pgdir, USER_BASE + i * PG_SIZE, user_page_perm(true)) && mapped;
        }
        constexpr const char* path = "/mnt/STATE.BIN";
        const bool copied = mapped && vmm::copy_to_user(&memory, USER_BASE, path, strlen(path) + 1) == Error::None &&
                            vmm::copy_to_user(&memory, USER_BASE + PG_SIZE, bytes.data(), bytes.size()) == Error::None;
        TEST_ASSERT(copied, "Copied path and multi-page data before starting writer");
        if (!copied) {
            TEST_END();
            return;
        }
    }
    DrainState state{&volume.disk, syscall ? &memory : nullptr, bytes.data()};
    volume.disk.hold_next_write(state.started, state.allow_write);
    auto writer = sched::kernel_thread(write_worker, &state);
    TEST_ASSERT(writer.ok(), "Started I/O worker");
    if (!writer.ok()) {
        volume.disk.cancel_hold();
        TEST_END();
        return;
    }
    state.started.down();
    TEST_ASSERT(volume.disk.has_held_write(), "Request is blocked inside the device backend");
    if (!volume.disk.has_held_write()) {
        int code{};
        wait_child(writer.value(), &code);
        volume.disk.cancel_hold();
        TEST_END();
        return;
    }
    auto shutdown = sched::kernel_thread(shutdown_worker, &state);
    TEST_ASSERT(shutdown.ok(), "Started shutdown coordinator");
    if (!shutdown.ok()) {
        state.allow_write.up();
        int code{};
        wait_child(writer.value(), &code);
        volume.disk.cancel_hold();
        TEST_END();
        return;
    }
    for (int i = 0; i < 1000; ++i) {
        if (syscall ? vfs::is_frozen() : BlockManager::is_frozen())
            break;
        sched::schedule();
    }
    TEST_ASSERT((syscall ? vfs::is_frozen() : BlockManager::is_frozen()) && state.prepared.count() == 0,
                "Preparation cannot pass an admitted request still in flight");
    TEST_ASSERT(vfs::create("/mnt/LATE.TXT") == Error::Busy, "New filesystem changes are rejected while draining");
    state.allow_write.up();
    state.prepared.down();
    int write_code{};
    wait_child(writer.value(), &write_code);
    TEST_ASSERT(state.error == Error::None && write_code == 0 &&
                    state.written == static_cast<long>(syscall ? WRITE_SIZE : 512),
                "Original request completes fully before shutdown preparation returns");
    TEST_ASSERT(volume.disk.is_durable(), "Cache barrier covers final data and metadata after request completion");
    state.release_shutdown.up();
    int shutdown_code{};
    wait_child(shutdown.value(), &shutdown_code);
    TEST_ASSERT(shutdown_code == 0 && !vfs::is_frozen() && !BlockManager::is_frozen(),
                "Coordinator releases both gates after cancellation");
    volume.disk.cancel_hold();
    TEST_END();
}

}  // namespace

namespace storage_sync_test {
void test() {
    tests_passed = tests_failed = 0;
    test_sync_persistence();
    test_flush_failure_retry();
    test_metadata_sync_failure();
    test_flush_capability();
    test_failed_shutdown();
    test_prepared_shutdown();
    test_reentrant_shutdown();
    test_drain(true);
    test_drain(false);
    TEST_SUMMARY("P0-2 storage sync");
}
}  // namespace storage_sync_test
