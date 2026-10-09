#include "test/test_defs.h"
#include "block/blk.h"
#include "exec/exec.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"
#include "drivers/intr.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "sched/sched.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

class FailureFile : public vfs::File {
public:
    enum class Reply { Stat, Read, Short, Negative, Oversized, InvalidElf, Directory, Empty, Large };
    FailureFile(Reply reply, int& closed) : reply_(reply), closed_(closed) {}
    ~FailureFile() override { ++closed_; }
    Error stat(vfs::Stat* st) override {
        if (reply_ == Reply::Stat) {
            return Error::Busy;
        }
        st->set(reply_ == Reply::Directory ? vfs::NodeType::Directory : vfs::NodeType::File,
                reply_ == Reply::Empty ? 0 : (reply_ == Reply::Large ? exec::MAX_BINARY_SIZE + 1 : 128), 0);
        return Error::None;
    }
    Result<int> read(void* buf, size_t size, size_t) override {
        switch (reply_) {
            case Reply::Read: return Error::Timeout;
            case Reply::Short: return static_cast<int>(size - 1);
            case Reply::Negative: return -1;
            case Reply::Oversized: return static_cast<int>(size + 1);
            default: memset(buf, 0, size); return static_cast<int>(size);
        }
    }
    Result<int> write(const void*, size_t, size_t) override { return Error::NotSupported; }

private:
    Reply reply_;
    int& closed_;
};

class FailureFs : public vfs::FileSystem {
public:
    static vfs::FileSystem* create() { return new (sys::nothrow) FailureFs(); }
    static Error register_type() {
        if (!registered_) {
            TRY(vfs::register_fs("exec-failure-test", create));
            registered_ = true;
        }
        return Error::None;
    }
    static int closed() { return mounted_->closed_; }
    Error mount(BlockDevice*) override {
        mounted_ = this;
        return Error::None;
    }
    void unmount() override { mounted_ = nullptr; }
    Result<vfs::FileHandle> open(const char* path) override {
        if (strcmp(path, "open") == 0) {
            return Error::NoDevice;
        }
        const char* names[] = {"stat", "read", "short", "negative", "oversized", "elf", "directory", "empty", "large"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            if (strcmp(path, names[i]) == 0) {
                vfs::FileHandle file(new (sys::nothrow) FailureFile(static_cast<FailureFile::Reply>(i), closed_));
                ENSURE(file, Error::NoMem);
                return file;
            }
        }
        return Error::NotFound;
    }
    Error stat(const char*, vfs::Stat*) override { return Error::NotSupported; }
    Result<int> readdir(const char*, vfs::DirVisitor&) override { return 0; }
    void print() override {}

private:
    inline static FailureFs* mounted_{};
    inline static bool registered_{};
    int closed_{};
};

void test_exec_errors() {
    TEST_START("exec preserves errors and closes resources on every failed image");
    if (vfs::is_mounted("/mnt")) {
        cprintf("  [SKIP] /mnt already in use\n");
        TEST_END();
        return;
    }
    Error registered = FailureFs::register_type();
    Error mounted = registered == Error::None ? vfs::mount("/mnt", nullptr, "exec-failure-test") : registered;
    TEST_ASSERT(mounted == Error::None, "Mounted exec failure fixture");
    if (mounted != Error::None) {
        TEST_END();
        return;
    }
    auto null_path = exec::exec(nullptr);
    auto missing = exec::exec("/mnt/missing");
    auto open = exec::exec("/mnt/open");
    TEST_ASSERT(!null_path.ok() && null_path.error() == Error::Invalid, "Null path returns Invalid");
    TEST_ASSERT(!missing.ok() && missing.error() == Error::NotFound, "Missing file preserves NotFound");
    TEST_ASSERT(!open.ok() && open.error() == Error::NoDevice, "Open failure preserves NoDevice");
    const char* paths[] = {"/mnt/stat", "/mnt/read",      "/mnt/short", "/mnt/negative", "/mnt/oversized",
                           "/mnt/elf",  "/mnt/directory", "/mnt/empty", "/mnt/large"};
    const Error errors[] = {Error::Busy,    Error::Timeout, Error::Io,      Error::Io,     Error::Io,
                            Error::Invalid, Error::Invalid, Error::Invalid, Error::Invalid};
    {
        intr::Guard guard;
        const size_t before = pmm::free_page_count();
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            auto result = exec::exec(paths[i]);
            TEST_ASSERT(!result.ok() && result.error() == errors[i], paths[i]);
            TEST_ASSERT(FailureFs::closed() == static_cast<int>(i + 1), "Failed exec closes its file exactly once");
            TEST_ASSERT(pmm::free_page_count() == before, "Failed exec releases buffer and prepared address space");
        }
    }
    TEST_ASSERT(vfs::umount("/mnt") == Error::None, "Failed exec leaves no open mount references");
    TEST_END();
}

}  // namespace

// ============================================================================
// Helper: mount second disk at /mnt (userdata.img)
// The first disk is the boot disk; the second is userdata.
// ============================================================================

// Count attached Disk-type block devices.
static int count_disks() {
    int count = BlockManager::device_count();
    int disks = 0;
    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::find_device(i);
        if (dev && dev->type == blk::DeviceType::Disk) {
            disks++;
        }
    }
    return disks;
}

static bool ensure_mnt_mounted() {
    if (vfs::is_mounted("/mnt")) {
        return true;
    }

    int count = BlockManager::device_count();
    int disk_index = 0;
    for (int i = 0; i < count; i++) {
        BlockDevice* dev = BlockManager::find_device(i);
        if (!dev || dev->type != blk::DeviceType::Disk) {
            continue;
        }
        // Skip the first disk (boot/system), mount the second
        if (disk_index++ < 1) {
            continue;
        }
        if (vfs::mount("/mnt", dev, "fat") == Error::None) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Test: exec a zcc-compiled user program from /mnt
// ============================================================================

static void test_exec_hello() {
    TEST_START("exec — run hello ELF from userdata disk");

    // Some targets (e.g. aarch64/riscv64 single-disk QEMU configs) have no
    // separate userdata disk. Skip the test in that case so CI still passes.
    if (count_disks() < 2) {
        cprintf("  [SKIP] no userdata disk attached\n");
        TEST_END();
        return;
    }

    bool mounted = ensure_mnt_mounted();
    TEST_ASSERT(mounted, "Userdata disk mounted at /mnt");
    if (!mounted) {
        TEST_END();
        return;
    }

    // Check the file exists
    vfs::File* probe = nullptr;
    const char* path = "/mnt/ZHELLO.ELF";
    Error rc = vfs::open(path, &probe);
    if (rc == Error::NotFound) {
        // The native assembly hello remains available without the zcc submodule.
        path = "/mnt/HELLO.ELF";
        rc = vfs::open(path, &probe);
    }
    TEST_ASSERT(rc == Error::None && probe != nullptr, "Hello ELF found on disk");
    if (probe) {
        vfs::close(probe);
    }
    if (rc != Error::None) {
        TEST_END();
        return;
    }

    // Execute and wait for exit
    auto pid_r = exec::exec(path);
    TEST_ASSERT(pid_r.ok(), "exec() returned valid PID");
    if (!pid_r.ok()) {
        TEST_END();
        return;
    }

    int exit_code = -1;
    auto wait_r = sched::wait(pid_r.value(), &exit_code);
    TEST_ASSERT(wait_r.ok(), "sched::wait() succeeded");
    TEST_ASSERT(exit_code == 0, "User program exited with code 0");

    TEST_END();
}

// ============================================================================

namespace exec_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_exec_errors();
    test_exec_hello();

    TEST_SUMMARY("Exec (E2E)");
}

}  // namespace exec_test
