#include "fs/vfs_fs.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include "lib/stdarg.h"
#include "lib/stdio.h"

extern "C" int vprintf(const char*, va_list);
extern "C" void* malloc(size_t);
extern "C" void free(void*);
extern "C" [[noreturn]] void exit(int);

namespace {

class VfsFixture {
public:
    VfsFixture() { active_ = this; }
    ~VfsFixture() {
        assert(guards_ == 0 && allocations_ == 0 && files_ == 0 && filesystems_ == 0);
        active_ = nullptr;
    }
    static VfsFixture& active() { return *active_; }
    // Real devfs registers its type during static initialization, before main.
    static void enter_guard() {
        if (active_)
            ++active_->guards_;
    }
    static void leave_guard() {
        if (active_)
            assert(--active_->guards_ >= 0);
    }
    void check_unguarded() const { assert(guards_ == 0); }
    void* allocate(size_t size) {
        check_unguarded();
        if (allocation_fails)
            return nullptr;
        void* ptr = malloc(size);
        assert(ptr);
        ++allocations_;
        return ptr;
    }
    void deallocate(void* ptr) {
        if (ptr) {
            check_unguarded();
            assert(allocations_ > 0);
            --allocations_;
            free(ptr);
        }
    }
    void fs_created() { ++filesystems_; }
    void fs_destroyed(const char* point) {
        check_transition(point);
        assert(files_ == 0 && filesystems_ > 0);
        --filesystems_;
    }
    void file_created() { ++files_; }
    void file_destroyed(const char* point) {
        check_pinned(point);
        assert(files_ > 0);
        --files_;
    }
    void check_pinned(const char* point) {
        check_unguarded();
        assert(vfs::is_mounted(point));
        assert(vfs::umount(point) == Error::Busy);
        ++pin_checks;
    }
    void check_transition(const char* point) {
        check_unguarded();
        assert(!vfs::is_mounted(point));
        assert(vfs::mount(point, nullptr, "contract") == Error::Busy);
        assert(vfs::umount(point) == Error::Busy);
        vfs::Stat st;
        assert(vfs::stat(point, &st) == Error::NotFound);
        ++transition_checks;
    }
    void check_path(const char* point, const char* path) {
        check_pinned(point);
        if (expected_path)
            assert(strcmp(path, expected_path) == 0);
    }
    const char* mount_point{"/mnt"};
    const char* expected_path{};
    bool allocation_fails{};
    bool mount_fails{};
    int pin_checks{};
    int transition_checks{};
    int files() const { return files_; }
    int filesystems() const { return filesystems_; }

private:
    inline static VfsFixture* active_{};
    int guards_{};
    int allocations_{};
    int files_{};
    int filesystems_{};
};

class ContractFile : public vfs::File {
public:
    explicit ContractFile(const char* point) : mount_point_(point) { VfsFixture::active().file_created(); }
    ~ContractFile() override { VfsFixture::active().file_destroyed(mount_point_); }
    Result<int> read(void*, size_t, size_t) override {
        VfsFixture::active().check_pinned(mount_point_);
        return 1;
    }
    Result<int> write(const void*, size_t, size_t) override { return Error::NotSupported; }
    Error stat(vfs::Stat*) override { return Error::NotSupported; }

private:
    const char* mount_point_;
};

class ContractFs : public vfs::FileSystem {
public:
    ContractFs() : mount_point_(VfsFixture::active().mount_point) { VfsFixture::active().fs_created(); }
    ~ContractFs() override { VfsFixture::active().fs_destroyed(mount_point_); }
    Error mount(BlockDevice*) override {
        VfsFixture::active().check_transition(mount_point_);
        return VfsFixture::active().mount_fails ? Error::Io : Error::None;
    }
    void unmount() override { VfsFixture::active().check_transition(mount_point_); }
    Result<vfs::FileHandle> open(const char* path) override {
        VfsFixture::active().check_path(mount_point_, path);
        if (strcmp(path, "empty") == 0)
            return vfs::FileHandle{};
        if (strcmp(path, "missing") == 0)
            return Error::NotFound;
        vfs::FileHandle file(new (sys::nothrow) ContractFile(mount_point_));
        ENSURE(file, Error::NoMem);
        if (strcmp(path, "partial") == 0)
            return Error::Timeout;
        return file;
    }
    Error stat(const char* path, vfs::Stat*) override {
        VfsFixture::active().check_path(mount_point_, path);
        return Error::Timeout;
    }
    Result<int> readdir(const char* path, vfs::DirVisitor& visitor) override {
        VfsFixture::active().check_path(mount_point_, path);
        vfs::DirEntry entry("file", vfs::NodeType::File, 1, 0);
        visitor.visit(entry);
        return Error::Io;
    }
    Error mkdir(const char* path) override {
        VfsFixture::active().check_path(mount_point_, path);
        return Error::NoMem;
    }
    Error create(const char* path) override {
        VfsFixture::active().check_path(mount_point_, path);
        return Error::Full;
    }
    Error unlink(const char* path) override {
        VfsFixture::active().check_path(mount_point_, path);
        return Error::Io;
    }
    Error rmdir(const char* path) override {
        VfsFixture::active().check_path(mount_point_, path);
        return Error::NotEmpty;
    }
    void print() override { VfsFixture::active().check_pinned(mount_point_); }

private:
    const char* mount_point_;
};

vfs::FileSystem* create_fs() {
    return new (sys::nothrow) ContractFs();
}

Result<vfs::FileHandle> create_device() {
    VfsFixture::active().check_unguarded();
    vfs::FileHandle file(new (sys::nothrow) ContractFile("/dev"));
    ENSURE(file, Error::NoMem);
    return file;
}

Result<vfs::FileHandle> fail_device() {
    auto file = TRY(create_device());
    return Error::Timeout;
}

Result<vfs::FileHandle> empty_device() {
    return vfs::FileHandle{};
}

class UnmountVisitor : public vfs::DirVisitor {
public:
    int visit(const vfs::DirEntry&) override {
        VfsFixture::active().check_pinned("/mnt");
        ++visits;
        return 0;
    }
    int visits{};
};

void test_operations(VfsFixture& fixture) {
    assert(vfs::mount("/mnt", nullptr, "contract") == Error::None);
    vfs::Stat st;
    fixture.expected_path = "file";
    assert(vfs::stat("/mnt///file", &st) == Error::Timeout);
    UnmountVisitor visitor;
    auto entries = vfs::readdir("/mnt/file", visitor);
    assert(!entries.ok() && entries.error() == Error::Io && visitor.visits == 1);
    assert(vfs::mkdir("/mnt/file") == Error::NoMem);
    assert(vfs::create("/mnt/file") == Error::Full);
    assert(vfs::unlink("/mnt/file") == Error::Io);
    assert(vfs::rmdir("/mnt/file") == Error::NotEmpty);
    vfs::print_mount_info("/mnt");
    assert(fixture.pin_checks == 8);
    assert(vfs::umount("/mnt") == Error::None);
}

void test_open(VfsFixture& fixture, const char* mode) {
    assert(vfs::mount("/mnt", nullptr, "contract") == Error::None);
    vfs::File* raw = reinterpret_cast<vfs::File*>(1);
    if (strcmp(mode, "files") == 0) {
        assert(vfs::open("/mnt/file", &raw) == Error::None && raw);
        auto result = vfs::open("/mnt/file");
        assert(result.ok());
        auto handle = result.release_value();
        assert(vfs::umount("/mnt") == Error::Busy && fixture.files() == 2);
        char byte;
        auto read = vfs::read(raw, &byte, 1, 0);
        assert(read.ok() && read.value() == 1);
        delete raw;
        assert(vfs::umount("/mnt") == Error::Busy);
        handle.reset();
    } else if (strcmp(mode, "partial") == 0) {
        assert(vfs::open("/mnt/partial", &raw) == Error::Timeout && !raw);
        auto result = vfs::open("/mnt/partial");
        assert(!result.ok() && result.error() == Error::Timeout && fixture.files() == 0);
    } else if (strcmp(mode, "empty") == 0) {
        assert(vfs::open("/mnt/empty", &raw) == Error::Io && !raw);
    } else {
        fixture.allocation_fails = true;
        assert(vfs::open("/mnt/file", &raw) == Error::NoMem && !raw);
        fixture.allocation_fails = false;
        assert(vfs::open("/mnt/missing", &raw) == Error::NotFound && !raw);
        assert(vfs::open(nullptr, &raw) == Error::Invalid && !raw);
        assert(vfs::open("/mnt", &raw) == Error::Invalid && !raw);
        assert(vfs::open("/mnt/file", nullptr) == Error::Invalid);
    }
    assert(vfs::umount("/mnt") == Error::None);
}

void test_mount(VfsFixture& fixture) {
    assert(vfs::mount(nullptr, nullptr, "contract") == Error::Invalid);
    assert(vfs::umount(nullptr) == Error::Invalid);
    assert(vfs::mount("/other", nullptr, "contract") == Error::NotFound);
    assert(vfs::mount("/mnt", nullptr, "unknown") == Error::NotFound);
    fixture.allocation_fails = true;
    assert(vfs::mount("/mnt", nullptr, "contract") == Error::NoMem);
    fixture.allocation_fails = false;
    fixture.mount_fails = true;
    assert(vfs::mount("/mnt", nullptr, "contract") == Error::Io && fixture.filesystems() == 0);
    fixture.mount_fails = false;
    assert(vfs::mount("/mnt", nullptr, "contract") == Error::None);
    assert(vfs::mount("/mnt", nullptr, "contract") == Error::Exists && fixture.filesystems() == 1);
    assert(vfs::umount("/mnt") == Error::None);
    assert(vfs::umount("/mnt") == Error::NotFound);
    // Every failure and teardown releases the reservation for a later mount.
    assert(vfs::mount("/mnt", nullptr, "contract") == Error::None);
    assert(vfs::umount("/mnt") == Error::None);
    assert(fixture.transition_checks == 8);
}

void test_paths(VfsFixture& fixture) {
    vfs::Stat st;
    assert(vfs::stat(nullptr, &st) == Error::Invalid);
    assert(vfs::stat("/mnt/file", &st) == Error::NotFound);
    constexpr const char* points[] = {"/", "/dev", "/mnt"};
    for (const char* point : points) {
        fixture.mount_point = point;
        assert(vfs::mount(point, nullptr, "contract") == Error::None);
    }
    fixture.expected_path = "";
    constexpr const char* directories[] = {"/", "/dev", "/dev///", "/mnt", "/mnt///"};
    for (const char* path : directories)
        assert(vfs::stat(path, &st) == Error::Timeout);
    fixture.expected_path = "file";
    constexpr const char* files[] = {"/file", "file", "/dev/file", "/mnt/file", "/mnt///file"};
    for (const char* path : files)
        assert(vfs::stat(path, &st) == Error::Timeout);
    fixture.expected_path = "devx/file";
    assert(vfs::stat("/devx/file", &st) == Error::Timeout);
    fixture.expected_path = "mntx/file";
    assert(vfs::stat("/mntx/file", &st) == Error::Timeout);
    for (const char* point : points)
        assert(vfs::umount(point) == Error::None);
}

class RegisteringVisitor : public vfs::DirVisitor {
public:
    int visit(const vfs::DirEntry& entry) override {
        auto& fixture = VfsFixture::active();
        fixture.check_pinned("/dev");
        assert(strcmp(entry.name, "late") != 0);
        if (visits++ == 0) {
            assert(vfs::register_char_dev("late", create_device) == Error::None);
        }
        return 0;
    }
    int visits{};
};

void test_devices(VfsFixture& fixture) {
    assert(vfs::register_char_dev(nullptr, create_device) == Error::Invalid);
    assert(vfs::register_char_dev("", create_device) == Error::Invalid);
    assert(vfs::register_char_dev("file", nullptr) == Error::Invalid);
    assert(vfs::register_char_dev("file", create_device) == Error::None);
    assert(vfs::register_char_dev("file", fail_device) == Error::Exists);
    assert(vfs::register_char_dev("partial", fail_device) == Error::None);
    assert(vfs::register_char_dev("empty", empty_device) == Error::None);
    assert(vfs::mount("/dev", nullptr, "devfs") == Error::None);
    {
        auto result = vfs::open("/dev/file");
        assert(result.ok() && fixture.files() == 1);
        assert(vfs::umount("/dev") == Error::Busy);
    }
    auto failure = vfs::open("/dev/partial");
    assert(!failure.ok() && failure.error() == Error::Timeout && fixture.files() == 0);
    auto empty = vfs::open("/dev/empty");
    assert(!empty.ok() && empty.error() == Error::Io);
    fixture.allocation_fails = true;
    auto no_memory = vfs::open("/dev/file");
    assert(!no_memory.ok() && no_memory.error() == Error::NoMem);
    fixture.allocation_fails = false;
    auto missing = vfs::open("/dev/missing");
    assert(!missing.ok() && missing.error() == Error::NotFound);
    RegisteringVisitor visitor;
    auto entries = vfs::readdir("/dev", visitor);
    assert(entries.ok() && entries.value() == 3 && visitor.visits == 3);
    vfs::Stat st;
    assert(vfs::stat("/dev/late", &st) == Error::None && st.type == vfs::NodeType::CharDevice);
    constexpr const char* names[] = {"fifth", "sixth", "seventh", "eighth"};
    for (const char* name : names)
        assert(vfs::register_char_dev(name, create_device) == Error::None);
    assert(vfs::register_char_dev("ninth", create_device) == Error::Full);
    assert(vfs::register_char_dev("file", create_device) == Error::Exists);
    assert(vfs::umount("/dev") == Error::None);
}

}  // namespace

int cprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}
void panic_at(const char* file, int line, const char* format, ...) {
    cprintf("assertion at %s:%d: ", file, line);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    exit(77);
}
void* operator new(size_t size, const sys::nothrow_t&) noexcept {
    return VfsFixture::active().allocate(size);
}
void operator delete(void* ptr) noexcept {
    VfsFixture::active().deallocate(ptr);
}
namespace intr {
Guard::Guard() {
    VfsFixture::enter_guard();
}
Guard::~Guard() {
    VfsFixture::leave_guard();
}
}  // namespace intr

extern "C" int main(int argc, char** argv) {
    assert(argc == 2);
    VfsFixture fixture;
    assert(vfs::register_fs("contract", create_fs) == Error::None);
    const char* mode = argv[1];
    if (strcmp(mode, "operations") == 0)
        test_operations(fixture);
    else if (strcmp(mode, "mount") == 0)
        test_mount(fixture);
    else if (strcmp(mode, "paths") == 0)
        test_paths(fixture);
    else if (strcmp(mode, "devices") == 0)
        test_devices(fixture);
    else
        test_open(fixture, mode);
    cprintf("[OK] VFS resource contract: %s\n", mode);
    return 0;
}
