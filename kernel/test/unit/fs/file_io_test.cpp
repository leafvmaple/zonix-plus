#include "test/test_defs.h"
#include "test/task_access.h"
#include "test/unit/fs/fat_image.h"
#include "fs/vfs.h"
#include "fs/vfs_fs.h"
#include "exec/exec.h"
#include "lib/kernel_buffer.h"
#include "lib/semaphore.h"
#include "mm/page_table.h"
#include "drivers/intr.h"
#include "trap/trap.h"
#include <abi/syscall.h>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

class Volume {
public:
    explicit Volume(uint8_t cluster_sectors = 1, uint32_t clusters = 32, uint8_t copies = 1)
        : image(cluster_sectors, clusters, copies) {
        if (image.ready()) {
            image.clear_files();
            status_ = mount();
        }
    }
    ~Volume() {
        if (mounted_) {
            assert(unmount() == Error::None);
        }
    }
    bool ready() const { return status_ == Error::None; }
    Error mount() {
        Error error = vfs::mount("/mnt", &image, "fat");
        if (error == Error::None) {
            mounted_ = true;
        }
        return error;
    }
    Error unmount() {
        Error error = vfs::umount("/mnt");
        if (error == Error::None) {
            mounted_ = false;
        }
        return error;
    }

    fat_test::FatImage image;

private:
    Error status_{Error::NoMem};
    bool mounted_{};
};

vfs::FileHandle open_file(const char* path, int flags) {
    auto result = vfs::open(path, flags);
    return result.ok() ? result.release_value() : vfs::FileHandle{};
}

bool has_size(vfs::File* file, uint32_t size) {
    vfs::Stat st{};
    return file && file->stat(&st) == Error::None && st.type == vfs::NodeType::File && st.size == size;
}

bool wrote(vfs::File* file, const void* bytes, size_t size, size_t offset = 0) {
    auto result = vfs::write(file, bytes, size, offset);
    return result.ok() && result.value() == static_cast<int>(size);
}

bool contents(vfs::File* file, const void* expected, size_t size) {
    auto allocation = KernelBuffer::alloc(size);
    if (!allocation.ok()) {
        return false;
    }
    auto bytes = allocation.release_value();
    auto result = vfs::read(file, bytes.data(), size, 0);
    return result.ok() && result.value() == static_cast<int>(size) && memcmp(bytes.data(), expected, size) == 0;
}

void fill_pattern(uint8_t* bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<uint8_t>(i * 37 + i / 251);
    }
}

void test_create_and_growth() {
    TEST_START("P0-1: create, cross-cluster overwrite/growth and reopen on 512B/8KB clusters");
    constexpr uint8_t cluster_sectors[] = {1, 16};
    for (uint8_t sectors : cluster_sectors) {
        Volume volume(sectors, 8, 2);
        TEST_ASSERT(volume.ready(), "Mounted isolated two-FAT volume");
        if (!volume.ready()) {
            continue;
        }
        auto file = open_file("/mnt/DATA.BIN", O_CREAT | O_EXCL | O_RDWR);
        const size_t size = volume.image.cluster_bytes() * 2 + 37;
        auto allocation = KernelBuffer::alloc(size);
        TEST_ASSERT(file && allocation.ok(), "Created empty file and allocated payload");
        if (!file || !allocation.ok()) {
            continue;
        }
        auto bytes = allocation.release_value();
        fill_pattern(bytes.data(), size);
        TEST_ASSERT(has_size(file.get(), 0), "New file starts empty without allocated data clusters");
        TEST_ASSERT(wrote(file.get(), bytes.data(), size) && has_size(file.get(), size),
                    "Single write grows through three clusters and publishes length");
        const size_t offset = volume.image.cluster_bytes() - 3;
        memset(bytes.data() + offset, 0xA5, 11);
        TEST_ASSERT(wrote(file.get(), bytes.data() + offset, 11, offset) && contents(file.get(), bytes.data(), size),
                    "Unaligned overwrite crosses a cluster without damaging adjacent bytes");
        file.reset();
        file = open_file("/mnt/DATA.BIN", O_RDONLY);
        TEST_ASSERT(has_size(file.get(), size) && contents(file.get(), bytes.data(), size),
                    "Close and reopen preserve every byte and file size");
        uint8_t byte{};
        auto eof = vfs::read(file.get(), &byte, 1, size + 10);
        TEST_ASSERT(eof.ok() && eof.value() == 0, "Reads beyond EOF return zero");
        file.reset();
        TEST_ASSERT(volume.unmount() == Error::None && volume.mount() == Error::None,
                    "Volume can be cleanly unmounted and mounted again");
        file = open_file("/mnt/DATA.BIN", O_RDONLY);
        TEST_ASSERT(has_size(file.get(), size) && contents(file.get(), bytes.data(), size),
                    "Fresh mount reconstructs length and the complete cluster chain");
        TEST_ASSERT(volume.image.link(3, 0) == volume.image.link(3, 1) &&
                        volume.image.link(4, 0) == volume.image.link(4, 1) &&
                        volume.image.link(5, 0) == volume.image.link(5, 1),
                    "Both FAT copies describe the same three-cluster file");
        file.reset();
        TEST_ASSERT(vfs::unlink("/mnt/DATA.BIN") == Error::None && volume.image.allocated_clusters() == 1,
                    "Unlink releases the complete file chain");
    }
    TEST_END();
}

void test_open_contract() {
    TEST_START("P0-1: open flags, access rights, invalid paths and FAT read-only attribute");
    Volume volume;
    TEST_ASSERT(volume.ready(), "Mounted open-contract volume");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto file = open_file("/mnt/FILE.TXT", O_CREAT | O_EXCL | O_RDWR);
    TEST_ASSERT(file && wrote(file.get(), "seed", 4), "Created initial file");
    if (!file) {
        TEST_END();
        return;
    }
    const int invalid[] = {O_ACCMODE, O_EXCL, O_RDONLY | O_TRUNC, O_RDONLY | O_APPEND, 0x40000000};
    for (int flags : invalid) {
        auto result = vfs::open("/mnt/FILE.TXT", flags);
        TEST_ASSERT(!result.ok() && result.error() == Error::Invalid && has_size(file.get(), 4),
                    "Invalid flags fail before changing existing contents");
    }
    auto duplicate = vfs::open("/mnt/FILE.TXT", O_CREAT | O_EXCL | O_RDWR);
    TEST_ASSERT(!duplicate.ok() && duplicate.error() == Error::Exists, "Exclusive create rejects existing file");
    auto existing = open_file("/mnt/FILE.TXT", O_CREAT | O_RDWR);
    TEST_ASSERT(existing && contents(existing.get(), "seed", 4), "Nonexclusive create opens without truncating");
    auto missing = vfs::open("/mnt/MISSING", O_RDONLY);
    TEST_ASSERT(!missing.ok() && missing.error() == Error::NotFound, "Open without create preserves NotFound");
    auto reader = open_file("/mnt/FILE.TXT", O_RDONLY);
    auto writer = open_file("/mnt/FILE.TXT", O_WRONLY);
    uint8_t byte{};
    auto read = vfs::read(writer.get(), &byte, 1, 0);
    auto write = vfs::write(reader.get(), &byte, 1, 0);
    auto empty_read = vfs::read(writer.get(), nullptr, 0, 0);
    auto empty_write = vfs::write(reader.get(), nullptr, 0, 0);
    TEST_ASSERT(reader && writer && !read.ok() && read.error() == Error::Access && !write.ok() &&
                    write.error() == Error::Access && !empty_read.ok() && !empty_write.ok(),
                "Read/write modes also protect zero-length I/O");
    const char* invalid_paths[] = {"/mnt/TOOLONGXX.TXT", "/mnt/GOOD.TOOLONG", "/mnt/BAD..TXT", "/mnt/BAD NAME",
                                   "/mnt/NEW/",          "/mnt/NEW/.",        "/mnt/NEW/.."};
    for (const char* path : invalid_paths) {
        auto result = vfs::open(path, O_CREAT | O_RDWR);
        TEST_ASSERT(!result.ok() && result.error() == Error::Invalid,
                    "Invalid 8.3 names and final path components are rejected");
    }
    TEST_ASSERT(volume.image.allocated_clusters() == 2, "Rejected opens do not allocate file storage");
    auto* entry = reinterpret_cast<FatDirEntry*>(volume.image.cluster_data(2));
    entry->attr |= FAT_ATTR_READ_ONLY;
    auto denied = vfs::open("/mnt/FILE.TXT", O_WRONLY | O_TRUNC);
    write = vfs::write(file.get(), "x", 1, 0);
    TEST_ASSERT(!denied.ok() && denied.error() == Error::Access && !write.ok() && write.error() == Error::Access,
                "On-disk read-only attribute protects truncation and previously opened writers");
    TEST_ASSERT(contents(reader.get(), "seed", 4), "Denied mutation preserves original bytes");
    reader.reset();
    writer.reset();
    existing.reset();
    file.reset();
    TEST_ASSERT(vfs::unlink("/mnt/FILE.TXT") == Error::Access, "Read-only file cannot be unlinked");
    TEST_END();
}

void test_truncate_and_handles() {
    TEST_START("P0-1: multiple handles observe truncation, gap filling and deletion ownership");
    Volume volume;
    TEST_ASSERT(volume.ready(), "Mounted multi-handle volume");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto allocation = KernelBuffer::alloc(2067);
    TEST_ASSERT(allocation.ok(), "Allocated truncate and gap verification buffers off the kernel stack");
    if (!allocation.ok()) {
        TEST_END();
        return;
    }
    auto storage = allocation.release_value();
    auto* bytes = storage.data();
    auto* expected = bytes + 1037;
    fill_pattern(bytes, 1037);
    auto writer = open_file("/mnt/SHARED.BIN", O_CREAT | O_RDWR);
    auto reader = open_file("/mnt/SHARED.BIN", O_RDONLY);
    TEST_ASSERT(writer && reader && wrote(writer.get(), bytes, 1037), "Wrote through first of two handles");
    TEST_ASSERT(has_size(reader.get(), 1037) && contents(reader.get(), bytes, 1037),
                "Already-open reader immediately observes growth and data");
    auto truncator = open_file("/mnt/SHARED.BIN", O_WRONLY | O_TRUNC);
    TEST_ASSERT(truncator && has_size(writer.get(), 0) && has_size(reader.get(), 0) &&
                    volume.image.allocated_clusters() == 1,
                "Truncate is visible to all handles and frees old clusters");
    TEST_ASSERT(wrote(writer.get(), "Z", 1, 1029), "Old writer can grow again beyond the new EOF");
    memset(expected, 0, 1030);
    expected[1029] = 'Z';
    TEST_ASSERT(has_size(reader.get(), 1030) && contents(reader.get(), expected, 1030),
                "Gap is zero-filled instead of exposing old truncated contents");
    TEST_ASSERT(vfs::unlink("/mnt/SHARED.BIN") == Error::Busy, "Open directory slot cannot be deleted or reused");
    truncator.reset();
    writer.reset();
    TEST_ASSERT(vfs::unlink("/mnt/SHARED.BIN") == Error::Busy, "Last reader still pins file identity");
    reader.reset();
    TEST_ASSERT(vfs::unlink("/mnt/SHARED.BIN") == Error::None && volume.image.allocated_clusters() == 1,
                "Last close permits deletion and complete storage reclamation");
    TEST_END();
}

void test_append() {
    TEST_START("P0-1: append uses fresh EOF and returns actual position for each handle");
    Volume volume;
    TEST_ASSERT(volume.ready(), "Mounted append volume");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto seed = open_file("/mnt/APP.TXT", O_CREAT | O_RDWR);
    auto first = open_file("/mnt/APP.TXT", O_WRONLY | O_APPEND);
    auto second = open_file("/mnt/APP.TXT", O_WRONLY | O_APPEND);
    TEST_ASSERT(seed && first && second && wrote(seed.get(), "hi", 2), "Opened two appenders before writing seed");
    size_t first_end{}, second_end{};
    auto a = vfs::write(first.get(), "A", 1, 999, &first_end);
    auto b = vfs::write(second.get(), "B", 1, 0, &second_end);
    TEST_ASSERT(a.ok() && a.value() == 1 && b.ok() && b.value() == 1 && first_end == 3 && second_end == 4 &&
                    contents(seed.get(), "hiAB", 4),
                "Append ignores stale supplied offsets and never overwrites another append");
    auto truncated = open_file("/mnt/APP.TXT", O_WRONLY | O_TRUNC | O_APPEND);
    TEST_ASSERT(truncated && has_size(seed.get(), 0) && wrote(second.get(), "C", 1) && contents(seed.get(), "C", 1),
                "Truncate plus append resets EOF for already-open appenders");
    TEST_END();
}

void test_full_and_data_failure() {
    TEST_START("P0-1: disk full and sector I/O failures preserve confirmed progress");
    for (int phase = 0; phase < 3; ++phase) {
        Volume volume(1, phase == 0 ? 4 : 8);
        TEST_ASSERT(volume.ready(), "Mounted space/error volume");
        if (!volume.ready()) {
            continue;
        }
        auto allocation = KernelBuffer::alloc(3072);
        TEST_ASSERT(allocation.ok(), "Allocated fault-injection buffers off the kernel stack");
        if (!allocation.ok()) {
            continue;
        }
        auto storage = allocation.release_value();
        auto* bytes = storage.data();
        auto* patch = bytes + 2048;
        fill_pattern(bytes, 2048);
        auto file = open_file("/mnt/FAIL.BIN", O_CREAT | O_RDWR);
        TEST_ASSERT(static_cast<bool>(file), "Created failure target");
        if (!file) {
            continue;
        }
        if (phase == 0) {
            size_t end = 9999;
            auto result = vfs::write(file.get(), bytes, 2048, 0, &end);
            auto full = vfs::write(file.get(), bytes, 1, 1536);
            TEST_ASSERT(result.ok() && result.value() == 1536 && end == 1536 && has_size(file.get(), 1536) &&
                            contents(file.get(), bytes, 1536) && !full.ok() && full.error() == Error::Full,
                        "Disk full returns committed prefix; next write returns Full without false success");
        } else if (phase == 1) {
            TEST_ASSERT(wrote(file.get(), bytes, 1024), "Seeded two-cluster file");
            memset(patch, 0xA5, 1024);
            volume.image.fail_write_at(volume.image.cluster_lba(4), 1, Error::Timeout);
            size_t end = 9999;
            auto result = vfs::write(file.get(), patch, 1024, 0, &end);
            memcpy(bytes, patch, 512);
            TEST_ASSERT(result.ok() && result.value() == 512 && end == 512 && contents(file.get(), bytes, 1024),
                        "Later sector failure reports only first sector and leaves untouched suffix intact");
            volume.image.fail_write_at(volume.image.cluster_lba(3), 1, Error::NoDevice);
            end = 9999;
            auto failed = vfs::write(file.get(), patch, 1, 0, &end);
            TEST_ASSERT(!failed.ok() && failed.error() == Error::NoDevice && end == 9999,
                        "First-sector failure keeps original error and caller position");
            volume.image.fail_read_at(volume.image.cluster_lba(3), Error::Timeout);
            auto read = vfs::read(file.get(), patch, 1, 0);
            TEST_ASSERT(!read.ok() && read.error() == Error::Timeout, "Read errors retain their device code");
            volume.image.fail_read_at(0, Error::None);
        } else {
            volume.image.fail_writes_at(volume.image.cluster_lba(3), Error::Io);
            auto result = vfs::write(file.get(), bytes, 1, 0);
            TEST_ASSERT(!result.ok() && result.error() == Error::Io && has_size(file.get(), 0) &&
                            volume.image.allocated_clusters() == 1,
                        "Failed cluster initialization reclaims unpublished allocation");
            volume.image.fail_writes_at(0, Error::None);
            TEST_ASSERT(wrote(file.get(), bytes, 1), "Write succeeds after transient allocation failure clears");
        }
        file.reset();
        TEST_ASSERT(vfs::unlink("/mnt/FAIL.BIN") == Error::None && volume.image.allocated_clusters() == 1,
                    "Failure paths retain a valid file that can be closed and removed");
    }
    TEST_END();
}

void test_metadata_failures() {
    TEST_START("P0-1: failed metadata publication retains referenced storage and permits retry");
    for (int phase = 0; phase < 3; ++phase) {
        Volume volume;
        TEST_ASSERT(volume.ready(), "Mounted metadata failure volume");
        if (!volume.ready()) {
            continue;
        }
        auto file = open_file("/mnt/META.BIN", O_CREAT | O_RDWR);
        TEST_ASSERT(static_cast<bool>(file), "Created metadata failure target");
        if (!file) {
            continue;
        }
        uint8_t bytes[525]{};
        fill_pattern(bytes, sizeof(bytes));
        if (phase == 0) {
            volume.image.fail_write_at(volume.image.cluster_lba(2), 1, Error::Timeout, 0, true);
            auto result = vfs::write(file.get(), bytes, 1, 0);
            TEST_ASSERT(!result.ok() && result.error() == Error::Timeout && has_size(file.get(), 0) &&
                            volume.image.link(3) == fat::FAT32_EOC_MAX,
                        "Uncertain first-cluster publication never frees a potentially live target");
            TEST_ASSERT(wrote(file.get(), bytes, 1) && volume.image.allocated_clusters() == 2,
                        "Retry reuses the published cluster instead of allocating twice");
        } else {
            TEST_ASSERT(wrote(file.get(), bytes, 512), "Seeded file before failed size growth");
            volume.image.fail_write_at(volume.image.cluster_lba(2), 1, Error::NoDevice, 0, phase == 2);
            size_t end = 9999;
            auto result = vfs::write(file.get(), bytes, sizeof(bytes), 0, &end);
            TEST_ASSERT(result.ok() && result.value() == 512 && end == 512 &&
                            has_size(file.get(), phase == 2 ? 525 : 512) && volume.image.link(3) == 4 &&
                            volume.image.link(4) == fat::FAT32_EOC_MAX,
                        "Failed size update reports confirmed prefix and keeps extension allocated");
            TEST_ASSERT(wrote(file.get(), bytes + 512, 13, 512) && contents(file.get(), bytes, sizeof(bytes)) &&
                            volume.image.allocated_clusters() == 3,
                        "Metadata retry reuses the extension and restores readable full contents");
        }
        file.reset();
        TEST_ASSERT(vfs::unlink("/mnt/META.BIN") == Error::None && volume.image.allocated_clusters() == 1,
                    "Published chains remain reclaimable after metadata failures");
    }
    TEST_END();
}

void test_tail_and_mirror_failures() {
    TEST_START("P0-1: tail rollback and dirty mirrored FAT survive failed writes and cache switches");
    {
        Volume volume;
        TEST_ASSERT(volume.ready(), "Mounted tail rollback volume");
        if (volume.ready()) {
            auto allocation = KernelBuffer::alloc(512);
            TEST_ASSERT(allocation.ok(), "Allocated tail rollback payload off the kernel stack");
            if (!allocation.ok()) {
                TEST_END();
                return;
            }
            auto bytes = allocation.release_value();
            memset(bytes.data(), 0, bytes.size());
            auto file = open_file("/mnt/TAIL.BIN", O_CREAT | O_RDWR);
            TEST_ASSERT(file && wrote(file.get(), bytes.data(), bytes.size()), "Seeded tail rollback target");
            volume.image.fail_write_at(volume.image.fat_lba(3), 2, Error::Timeout, 0, true);
            auto result = vfs::write(file.get(), "x", 1, 512);
            TEST_ASSERT(!result.ok() && result.error() == Error::Timeout && has_size(file.get(), 512) &&
                            volume.image.link(3) == fat::FAT32_EOC_MAX && volume.image.link(4) == fat::FAT32_FREE,
                        "Failed tail publication restores original link before freeing extension");
            TEST_ASSERT(wrote(file.get(), "x", 1, 512), "Retry succeeds without following a freed target");
        }
    }
    {
        Volume volume(1, 130, 2);
        TEST_ASSERT(volume.ready(), "Mounted multi-sector mirrored FAT volume");
        if (volume.ready()) {
            auto old = open_file("/mnt/OLD.BIN", O_CREAT | O_RDWR);
            TEST_ASSERT(old && wrote(old.get(), "o", 1), "Created low-FAT-sector file");
            old.reset();
            TEST_ASSERT(volume.unmount() == Error::None, "Detached filesystem before preparing occupied clusters");
            for (uint32_t cluster = 4; cluster < 128; ++cluster) {
                volume.image.set_link(cluster, fat::FAT32_EOC_MAX);
            }
            TEST_ASSERT(volume.mount() == Error::None, "Remounted occupied low-FAT-sector fixture");
            old = open_file("/mnt/OLD.BIN", O_RDWR);
            auto file = open_file("/mnt/HIGH.BIN", O_CREAT | O_RDWR);
            volume.image.fail_writes_at(volume.image.fat_lba(128, 1), Error::NoDevice);
            auto result = vfs::write(file.get(), "h", 1, 0);
            TEST_ASSERT(!result.ok() && result.error() == Error::NoDevice &&
                            volume.image.link(128, 0) == fat::FAT32_EOC_MAX &&
                            volume.image.link(128, 1) == fat::FAT32_FREE,
                        "Failed mirror leaves dirty high FAT sector and original device error");
            volume.image.fail_writes_at(0, Error::None);
            TEST_ASSERT(wrote(old.get(), "O", 1) && volume.image.link(128, 0) == volume.image.link(128, 1) &&
                            volume.image.link(128, 1) == fat::FAT32_EOC_MAX && contents(old.get(), "O", 1),
                        "Reading a different FAT sector retries the dirty mirror before replacing cache");
        }
    }
    TEST_END();
}

void test_truncate_cleanup_and_unmount() {
    TEST_START("P0-1: truncate cleanup failure preserves cursor and failed unmount retains mount");
    Volume volume;
    TEST_ASSERT(volume.ready(), "Mounted cleanup failure volume");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto allocation = KernelBuffer::alloc(1536);
    TEST_ASSERT(allocation.ok(), "Allocated cleanup failure payload");
    if (!allocation.ok()) {
        TEST_END();
        return;
    }
    auto bytes = allocation.release_value();
    fill_pattern(bytes.data(), bytes.size());
    auto file = open_file("/mnt/FREE.BIN", O_CREAT | O_RDWR);
    TEST_ASSERT(file && wrote(file.get(), bytes.data(), bytes.size()), "Seeded three-cluster chain");
    volume.image.fail_write_at(volume.image.cluster_lba(2), 1, Error::Timeout);
    auto denied = vfs::open("/mnt/FREE.BIN", O_WRONLY | O_TRUNC);
    TEST_ASSERT(!denied.ok() && denied.error() == Error::Timeout && contents(file.get(), bytes.data(), bytes.size()) &&
                    volume.image.allocated_clusters() == 4,
                "Failed truncate metadata write does not free the still-referenced chain");
    volume.image.fail_write_at(volume.image.fat_lba(3), 2, Error::Timeout, 0, true);
    auto truncated = vfs::open("/mnt/FREE.BIN", O_WRONLY | O_TRUNC);
    TEST_ASSERT(!truncated.ok() && truncated.error() == Error::Timeout && has_size(file.get(), 0) &&
                    volume.image.link(5) == fat::FAT32_EOC_MAX,
                "Cleanup failure detaches file but retains the next link after partially effective free");
    file.reset();
    volume.image.fail_writes_at(volume.image.fat_lba(3), Error::NoDevice);
    TEST_ASSERT(volume.unmount() == Error::NoDevice && vfs::is_mounted("/mnt") &&
                    vfs::mounted_device("/mnt") == volume.image.name,
                "Failed unmount preserves filesystem and original mount metadata");
    auto reader = open_file("/mnt/FREE.BIN", O_RDONLY);
    TEST_ASSERT(has_size(reader.get(), 0), "Republished mount remains usable after failed cleanup");
    reader.reset();
    volume.image.fail_writes_at(0, Error::None);
    TEST_ASSERT(volume.unmount() == Error::None && !vfs::is_mounted("/mnt") && volume.image.allocated_clusters() == 1,
                "Retry frees the saved next cluster rather than losing the rest of the chain");
    TEST_ASSERT(volume.mount() == Error::None, "Cleaned volume mounts again after failure recovery");
    reader = open_file("/mnt/FREE.BIN", O_RDONLY);
    TEST_ASSERT(has_size(reader.get(), 0), "Truncated size remains correct on fresh mount");
    TEST_END();
}

class CountEntries : public vfs::DirVisitor {
public:
    int visit(const vfs::DirEntry&) override {
        ++count;
        return 0;
    }
    int count{};
};

void test_directory_terminator() {
    TEST_START("P0-1: create moves end marker across sector and cluster boundaries");
    constexpr uint8_t cluster_sectors[] = {1, 2};
    for (uint8_t sectors : cluster_sectors) {
        Volume volume(sectors);
        TEST_ASSERT(volume.ready(), "Mounted directory boundary volume");
        if (!volume.ready()) {
            continue;
        }
        auto* entries = reinterpret_cast<FatDirEntry*>(volume.image.cluster_data(2));
        memset(entries, 0xE5, 15 * sizeof(FatDirEntry));
        entries[15] = {};
        if (sectors == 1) {
            volume.image.set_link(2, 3);
            volume.image.set_link(3, fat::FAT32_EOC_MAX);
        }
        auto* next = reinterpret_cast<FatDirEntry*>(sectors == 1 ? volume.image.cluster_data(3)
                                                                 : volume.image.cluster_data(2) + 512);
        *next = {};
        memcpy(next->name, "GHOST   ", 8);
        memcpy(next->ext, "TXT", 3);
        next->attr = FAT_ATTR_ARCHIVE;
        auto file = open_file("/mnt/NEW.TXT", O_CREAT | O_EXCL | O_RDWR);
        CountEntries visitor;
        auto listed = vfs::readdir("/mnt", visitor);
        vfs::Stat st{};
        TEST_ASSERT(file && listed.ok() && visitor.count == 1 && vfs::stat("/mnt/GHOST.TXT", &st) == Error::NotFound,
                    "Publishing over final slot does not resurrect stale entries in the next sector");
    }
    TEST_END();
}

constexpr uintptr_t USER_BASE = 0x400000;
constexpr size_t APPEND_SIZE = PG_SIZE + 37;

class AppendProbe : public vfs::File {
public:
    Result<int> read(void*, size_t, size_t) override { return Error::NotSupported; }
    Result<int> write(const void*, size_t, size_t) override { return Error::NotSupported; }
    Error stat(vfs::Stat*) override { return Error::NotSupported; }
    Result<int> append(const void* buf, size_t size, size_t* end_offset) override {
        ++calls_;
        const auto* bytes = static_cast<const uint8_t*>(buf);
        for (size_t i = 0; i < size; ++i) {
            data_valid_ = data_valid_ && bytes[i] == 0xA5;
        }
        position_ += size;
        *end_offset = position_;
        return static_cast<int>(size);
    }
    size_t calls() const { return calls_; }
    bool is_data_valid() const { return data_valid_; }

private:
    size_t calls_{};
    size_t position_{100};
    bool data_valid_{true};
};

class AppendProbeFs : public vfs::FileSystem {
public:
    Error mount(BlockDevice*) override { return Error::None; }
    Error unmount() override { return Error::None; }
    Error sync() override { return Error::None; }
    Result<vfs::FileHandle> open(const char*, int) override {
        vfs::FileHandle file(new (sys::nothrow) AppendProbe());
        ENSURE(file, Error::NoMem);
        return file;
    }
    Error stat(const char*, vfs::Stat*) override { return Error::NotSupported; }
    Result<int> readdir(const char*, vfs::DirVisitor&) override { return Error::NotSupported; }
    void print() override {}
};

vfs::FileSystem* create_append_probe_fs() {
    return new (sys::nothrow) AppendProbeFs();
}

Error prepare_user_memory(MemoryDesc& mm) {
    mm.pgdir = TRY(exec::create_user_pgdir());
    for (size_t i = 0; i < 3; ++i) {
        ENSURE(pmm::alloc_and_map_page(mm.pgdir, USER_BASE + i * PG_SIZE, user_page_perm(true)), Error::NoMem);
    }
    return Error::None;
}

long invoke(int nr, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2) {
    sys::unique_ptr<TrapFrame> tf(new (sys::nothrow) TrapFrame{});
    if (!tf)
        return -2;
    tf->set_syscall(nr, arg0, arg1, arg2);
    if (!trap::handle_syscall(tf.get())) {
        return -2;
    }
    return static_cast<long>(tf->syscall_return());
}

void test_append_syscall_boundary() {
    TEST_START("P0-1: multi-page append reaches backend as one operation");
    MemoryDesc mm;
    auto allocation = KernelBuffer::alloc(APPEND_SIZE);
    const Error memory = prepare_user_memory(mm);
    TEST_ASSERT(allocation.ok() && memory == Error::None, "Prepared append-call boundary fixture");
    if (!allocation.ok() || memory != Error::None) {
        TEST_END();
        return;
    }
    auto bytes = allocation.release_value();
    memset(bytes.data(), 0xA5, bytes.size());
    constexpr const char* path = "/mnt/probe";
    TEST_ASSERT(vmm::copy_to_user(&mm, USER_BASE, path, strlen(path) + 1) == Error::None &&
                    vmm::copy_to_user(&mm, USER_BASE + PG_SIZE, bytes.data(), bytes.size()) == Error::None,
                "Copied complete multi-page append payload");
    Error registered = vfs::register_fs("append-probe", create_append_probe_fs);
    const Error mounted = registered == Error::None || registered == Error::Exists
                              ? vfs::mount("/mnt", nullptr, "append-probe")
                              : registered;
    TEST_ASSERT(mounted == Error::None, "Mounted append operation probe");
    if (mounted != Error::None) {
        TEST_END();
        return;
    }
    {
        intr::Guard guard;
        ScopedTaskMemory memory_view(*sched::current(), mm);
        const int fd = static_cast<int>(invoke(NR_OPEN, USER_BASE, O_WRONLY | O_APPEND, 0));
        TEST_ASSERT(fd >= 0, "Opened append probe through common VFS path");
        if (fd >= 0) {
            const long count = invoke(NR_WRITE, fd, USER_BASE + PG_SIZE, APPEND_SIZE);
            auto* entry = sched::current()->files().get(fd);
            auto* probe = static_cast<AppendProbe*>(entry->file.get());
            TEST_ASSERT(count == static_cast<long>(APPEND_SIZE) && probe->calls() == 1 && probe->is_data_valid() &&
                            entry->offset == 100 + APPEND_SIZE,
                        "One syscall selects EOF exactly once, transfers every byte and publishes actual position");
            TEST_ASSERT(invoke(NR_CLOSE, fd, 0, 0) == 0, "Closed append operation probe");
        }
    }
    TEST_ASSERT(vfs::umount("/mnt") == Error::None, "Append operation probe releases mount references");
    TEST_END();
}

void test_syscall_contract() {
    TEST_START("P0-1: syscall open/write/append offsets, permissions and full FD table");
    Volume volume;
    MemoryDesc mm;
    auto allocation = KernelBuffer::alloc(APPEND_SIZE);
    const Error memory = prepare_user_memory(mm);
    TEST_ASSERT(volume.ready() && allocation.ok() && memory == Error::None,
                "Prepared isolated filesystem and user pages");
    if (!volume.ready() || !allocation.ok() || memory != Error::None) {
        TEST_END();
        return;
    }
    auto bytes = allocation.release_value();
    fill_pattern(bytes.data(), bytes.size());
    constexpr const char* path = "/mnt/SYS.BIN";
    constexpr uintptr_t user_data = USER_BASE + PG_SIZE - 17;
    TEST_ASSERT(vmm::copy_to_user(&mm, USER_BASE, path, strlen(path) + 1) == Error::None &&
                    vmm::copy_to_user(&mm, user_data, bytes.data(), bytes.size()) == Error::None,
                "Copied path and payload spanning three user pages");
    intr::Guard guard;
    ScopedTaskMemory memory_view(*sched::current(), mm);
    const int fd = static_cast<int>(invoke(NR_OPEN, USER_BASE, O_CREAT | O_EXCL | O_RDWR, 0));
    TEST_ASSERT(fd >= 0, "Open syscall creates writable file");
    if (fd < 0) {
        TEST_END();
        return;
    }
    auto* entry = sched::current()->files().get(fd);
    TEST_ASSERT(invoke(NR_WRITE, fd, user_data, APPEND_SIZE) == static_cast<long>(APPEND_SIZE) &&
                    entry->offset == APPEND_SIZE,
                "Cross-page syscall write advances by confirmed count");
    const int append_fd = static_cast<int>(invoke(NR_OPEN, USER_BASE, O_WRONLY | O_APPEND, 0));
    TEST_ASSERT(append_fd >= 0 && invoke(NR_WRITE, append_fd, user_data, APPEND_SIZE) == static_cast<long>(APPEND_SIZE),
                "Append syscall accepts a record larger than bounce-buffer page");
    if (append_fd >= 0) {
        auto* append_entry = sched::current()->files().get(append_fd);
        TEST_ASSERT(append_entry->offset == 2 * APPEND_SIZE && entry->offset == APPEND_SIZE,
                    "Append uses actual EOF while separately opened descriptor keeps its own offset");
        TEST_ASSERT(invoke(NR_READ, append_fd, user_data, 0) == -1, "Write-only syscall rejects even empty reads");
        TEST_ASSERT(invoke(NR_CLOSE, append_fd, 0, 0) == 0, "Closed append descriptor");
    }
    TEST_ASSERT(invoke(NR_CLOSE, fd, 0, 0) == 0, "Closed original descriptor");
    const int read_fd = static_cast<int>(invoke(NR_OPEN, USER_BASE, O_RDONLY, 0));
    TEST_ASSERT(read_fd >= 0 && invoke(NR_WRITE, read_fd, user_data, 0) == -1,
                "Read-only descriptor rejects empty writes");
    if (read_fd >= 0) {
        TEST_ASSERT(invoke(NR_READ, read_fd, user_data, APPEND_SIZE) == static_cast<long>(APPEND_SIZE) &&
                        vmm::copy_from_user(&mm, bytes.data(), user_data, APPEND_SIZE) == Error::None,
                    "Reopened descriptor reads data back through user copy boundary");
        bool valid = true;
        for (size_t i = 0; i < bytes.size(); ++i) {
            valid = valid && bytes.data()[i] == static_cast<uint8_t>(i * 37 + i / 251);
        }
        TEST_ASSERT(valid, "Syscall read preserves complete cross-page pattern");
        TEST_ASSERT(invoke(NR_CLOSE, read_fd, 0, 0) == 0, "Closed read descriptor");
    }
    TEST_ASSERT(invoke(NR_OPEN, USER_BASE, O_RDONLY | O_TRUNC, 0) == -1 &&
                    invoke(NR_OPEN, USER_BASE, O_RDWR, 0644) == -1 &&
                    invoke(NR_OPEN, USER_SPACE_TOP - 1, O_CREAT | O_RDWR, 0) == -1,
                "Invalid flags, reserved mode and invalid user path are rejected");
    TEST_ASSERT(invoke(NR_READ, fd::MAX_FD, 0, 0) == -1 && invoke(NR_WRITE, fd::MAX_FD, 0, 0) == -1,
                "Zero-length syscalls still reject invalid descriptors");
    sys::inplace_vector<int, fd::MAX_FD> filled;
    while (sched::current()->files().has_space()) {
        const int next = static_cast<int>(invoke(NR_OPEN, USER_BASE, O_RDONLY, 0));
        if (next < 0) {
            break;
        }
        assert(filled.try_push_back(next));
    }
    vfs::Stat st{};
    TEST_ASSERT(!sched::current()->files().has_space() && invoke(NR_OPEN, USER_BASE, O_WRONLY | O_TRUNC, 0) == -1 &&
                    vfs::stat(path, &st) == Error::None && st.size == 2 * APPEND_SIZE,
                "Full descriptor table rejects open before truncating existing file");
    for (int next : filled) {
        TEST_ASSERT(invoke(NR_CLOSE, next, 0, 0) == 0, "Released descriptor-table exhaustion fixture");
    }
    TEST_ASSERT(vfs::unlink(path) == Error::None && volume.image.allocated_clusters() == 1,
                "Syscall closes release every handle and file cluster");
    TEST_END();
}

struct AppendWork {
    Semaphore* ready;
    Semaphore* gate;
    uint8_t value;
};

int append_worker(void* raw) {
    auto& work = *static_cast<AppendWork*>(raw);
    MemoryDesc mm;
    auto allocation = KernelBuffer::alloc(APPEND_SIZE);
    bool ready = allocation.ok() && prepare_user_memory(mm) == Error::None;
    KernelBuffer bytes;
    if (allocation.ok()) {
        bytes = allocation.release_value();
        memset(bytes.data(), work.value, bytes.size());
    }
    constexpr const char* path = "/mnt/CONCUR.BIN";
    ready = ready && vmm::copy_to_user(&mm, USER_BASE, path, strlen(path) + 1) == Error::None &&
            vmm::copy_to_user(&mm, USER_BASE + PG_SIZE, bytes.data(), bytes.size()) == Error::None;
    work.ready->up();
    work.gate->down();
    if (!ready) {
        return 1;
    }
    int result = 1;
    {
        intr::Guard guard;
        const uintptr_t saved_root = arch_read_page_table_root();
        ScopedTaskMemory memory_view(*sched::current(), mm);
        const int fd = static_cast<int>(invoke(NR_OPEN, USER_BASE, O_WRONLY | O_APPEND, 0));
        if (fd >= 0) {
            const long count = invoke(NR_WRITE, fd, USER_BASE + PG_SIZE, APPEND_SIZE);
            auto* entry = sched::current()->files().get(fd);
            result = count == static_cast<long>(APPEND_SIZE) && entry &&
                             (entry->offset == APPEND_SIZE || entry->offset == 2 * APPEND_SIZE)
                         ? 0
                         : 1;
            if (invoke(NR_CLOSE, fd, 0, 0) != 0) {
                result = 1;
            }
        }
        // Scheduling may have activated the borrowed MM; restore before its owner is destroyed.
        arch_load_page_table_root(saved_root);
    }
    return result;
}

void yield_after_write() {
    sched::schedule();
}

void test_concurrent_append() {
    TEST_START("P0-1: concurrent multi-page append syscalls remain indivisible under forced switches");
    Volume volume;
    TEST_ASSERT(volume.ready(), "Mounted concurrent append volume");
    if (!volume.ready()) {
        TEST_END();
        return;
    }
    auto seed = open_file("/mnt/CONCUR.BIN", O_CREAT | O_RDWR);
    TEST_ASSERT(static_cast<bool>(seed), "Created concurrent append target");
    if (!seed) {
        TEST_END();
        return;
    }
    Semaphore ready, gate;
    AppendWork first{&ready, &gate, 'A'}, second{&ready, &gate, 'B'};
    const size_t free_pages = pmm::free_page_count();
    const int process_count = TaskManager::process_count();
    volume.image.set_write_hook(yield_after_write);
    auto a = sched::kernel_thread(append_worker, &first);
    auto b = sched::kernel_thread(append_worker, &second);
    TEST_ASSERT(a.ok() && b.ok(), "Created both append workers");
    if (a.ok())
        ready.down();
    if (b.ok())
        ready.down();
    if (a.ok())
        gate.up();
    if (b.ok())
        gate.up();
    Result<int>* children[] = {&a, &b};
    for (auto* child : children) {
        if (child->ok()) {
            int code = -1;
            auto waited = sched::wait(child->value(), &code);
            TEST_ASSERT(waited.ok() && waited.value() == child->value() && code == 0,
                        "Append worker returns correct byte count/offset and is fully reaped");
        }
    }
    volume.image.set_write_hook(nullptr);
    TEST_ASSERT(pmm::free_page_count() == free_pages && TaskManager::process_count() == process_count,
                "Reaped append workers return user pages, page tables, kernel stacks and tasks to baseline");
    auto allocation = KernelBuffer::alloc(2 * APPEND_SIZE);
    TEST_ASSERT(allocation.ok(), "Allocated concurrent append verification buffer");
    if (allocation.ok()) {
        auto bytes = allocation.release_value();
        auto result = vfs::read(seed.get(), bytes.data(), bytes.size(), 0);
        bool records = result.ok() && result.value() == static_cast<int>(bytes.size());
        if (records) {
            const uint8_t first_value = bytes.data()[0];
            const uint8_t second_value = bytes.data()[APPEND_SIZE];
            records = (first_value == 'A' && second_value == 'B') || (first_value == 'B' && second_value == 'A');
            for (size_t i = 0; i < APPEND_SIZE; ++i) {
                records = records && bytes.data()[i] == first_value && bytes.data()[APPEND_SIZE + i] == second_value;
            }
        }
        TEST_ASSERT(records && has_size(seed.get(), bytes.size()),
                    "Forced interleaving never splits, overwrites or loses either complete append record");
    }
    TEST_END();
}

void test_real_disk() {
    TEST_START("P0-1: real disk create/grow/truncate/append survives filesystem remount");
    BlockDevice* device = BlockManager::find_device(vfs::mounted_device("/"));
    TEST_ASSERT(device != nullptr, "Found QEMU-backed root filesystem device");
    if (!device) {
        TEST_END();
        return;
    }
    constexpr size_t size = 65536 + 37;  // Larger than the maximum supported FAT cluster.
    auto allocation = KernelBuffer::alloc(size + 3);
    TEST_ASSERT(allocation.ok(), "Allocated cross-cluster real-disk payload");
    if (!allocation.ok()) {
        TEST_END();
        return;
    }
    auto bytes = allocation.release_value();
    fill_pattern(bytes.data(), size);
    auto file = open_file("/P0IO.TMP", O_CREAT | O_TRUNC | O_RDWR);
    auto reader = open_file("/P0IO.TMP", O_RDONLY);
    TEST_ASSERT(file && reader && wrote(file.get(), bytes.data(), size) && has_size(reader.get(), size) &&
                    contents(reader.get(), bytes.data(), size),
                "Hardware block driver completes multi-cluster growth and live-reader refresh");
    auto append = open_file("/P0IO.TMP", O_WRONLY | O_APPEND);
    memcpy(bytes.data() + size, "end", 3);
    TEST_ASSERT(append && wrote(append.get(), "end", 3) && contents(reader.get(), bytes.data(), size + 3),
                "Real disk append is visible through previously opened reader");
    file.reset();
    reader.reset();
    append.reset();
    const Error unmounted = vfs::umount("/");
    const Error mounted = unmounted == Error::None ? vfs::mount("/", device, "fat") : unmounted;
    TEST_ASSERT(unmounted == Error::None && mounted == Error::None, "Remounted real disk after closing all handles");
    if (mounted == Error::None) {
        reader = open_file("/P0IO.TMP", O_RDONLY);
        TEST_ASSERT(has_size(reader.get(), size + 3) && contents(reader.get(), bytes.data(), size + 3),
                    "On-disk directory and FAT reconstruct exact contents after remount");
        file = open_file("/P0IO.TMP", O_WRONLY | O_TRUNC);
        TEST_ASSERT(file && has_size(reader.get(), 0) && wrote(file.get(), "new", 3) &&
                        contents(reader.get(), "new", 3),
                    "Real disk truncation releases old chain and accepts new data");
        file.reset();
        reader.reset();
        TEST_ASSERT(vfs::unlink("/P0IO.TMP") == Error::None, "Removed real-disk regression artifact");
    }
    TEST_END();
}

}  // namespace

namespace file_io_test {
void test() {
    tests_passed = tests_failed = 0;
    test_create_and_growth();
    test_open_contract();
    test_truncate_and_handles();
    test_append();
    test_full_and_data_failure();
    test_metadata_failures();
    test_tail_and_mirror_failures();
    test_truncate_cleanup_and_unmount();
    test_directory_terminator();
    test_append_syscall_boundary();
    test_syscall_contract();
    test_concurrent_append();
    test_real_disk();
    TEST_SUMMARY("P0-1 file I/O");
}
}  // namespace file_io_test
