#include "fs/fat.h"

#include <debug/assert.h>
#include <sys/inplace_vector.hpp>
#include <sys/array.hpp>
#include <sys/algorithm.hpp>
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/kernel_buffer.h"
#include <base/bpb.h>

namespace {

constexpr int SHORT_NAME_CAPACITY = 13;
// Limit displayed names to the existing directory-entry ABI.
constexpr int MAX_PART_LEN = 32;
constexpr int MAX_DEPTH = 16;
static char to_upper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 32) : ch;
}

template<size_t N>
static bool next_part(const char*& path, sys::array<char, N>& buf) {
    size_t len{};
    while (*path && *path != '/') {
        if (len + 1 >= N) {
            return false;
        }
        buf[len++] = to_upper(*path++);
    }
    buf[len] = '\0';
    while (*path == '/') {
        path++;
    }
    return true;
}

using PathParts = sys::inplace_vector<const char*, MAX_DEPTH>;

Error split_path(const char* path, PathParts& parts) {
    path = str_skip_char(path, '/');
    ENSURE(path && *path);
    while (*path) {
        const char* start = path;
        sys::array<char, MAX_PART_LEN> name{};
        ENSURE(next_part(path, name));
        if (!strcmp(name.data(), "."))
            continue;
        if (!strcmp(name.data(), "..")) {
            if (!parts.empty())
                parts.pop_back();
        } else {
            ENSURE(parts.try_push_back(start));
        }
    }
    ENSURE(!parts.empty());
    return Error::None;
}

bool name_matches(const char* name, const char* upper) {
    while (*name && to_upper(*name) == *upper) {
        ++name;
        ++upper;
    }
    return *name == '\0' && *upper == '\0';
}

bool short_name_matches(const FatDirEntry& entry, const char* upper) {
    char name[SHORT_NAME_CAPACITY];
    entry.get_filename(name, sizeof(name));
    return name_matches(name, upper);
}

class LongName {
public:
    void consume(const FatDirEntry& entry) {
        present_ = !entry.is_deleted() && entry.attr == FAT_ATTR_LFN;
        if (!present_) {
            reset();
            return;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(&entry);
        bool last = (bytes[0] & LAST_ENTRY) != 0;
        unsigned int ordinal = bytes[0] & ~LAST_ENTRY;
        if (last) {
            next_ = ordinal;
            length_ = ordinal * CHARS_PER_ENTRY;
            checksum_ = bytes[13];
        }
        if (ordinal == 0 || ordinal > MAX_ORDINAL || ordinal != next_ || bytes[12] != 0 || bytes[26] != 0 ||
            bytes[27] != 0 || bytes[13] != checksum_) {
            reset();
            return;
        }
        unsigned int position = (ordinal - 1) * CHARS_PER_ENTRY;
        for (unsigned int offset : NAME_OFFSETS) {
            unsigned int value = bytes[offset] | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
            if (position >= length_) {
                if (value != 0xFFFF) {
                    reset();
                    return;
                }
            } else if (value == 0 && last) {
                length_ = position;
            } else if (position >= name_.size() - 1 || value < 0x20 || value > 0x7E ||
                       strchr("\\/:*?\"<>|", static_cast<char>(value))) {
                reset();
                return;
            } else {
                name_[position] = static_cast<char>(value);
            }
            ++position;
        }
        --next_;
    }

    const char* name(const FatDirEntry& entry) {
        if (next_ != 0 || length_ == 0 || length_ >= name_.size() || name_[length_ - 1] == '.' ||
            name_[length_ - 1] == ' ' || checksum(entry) != checksum_)
            return nullptr;
        name_[length_] = '\0';
        return name_.data();
    }

    bool present() const { return present_; }
    void clear() {
        present_ = false;
        reset();
    }

private:
    static constexpr unsigned int LAST_ENTRY = 0x40;
    static constexpr sys::array<unsigned int, 13> NAME_OFFSETS{1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    static constexpr unsigned int CHARS_PER_ENTRY = NAME_OFFSETS.size();
    static constexpr unsigned int MAX_ORDINAL = (MAX_PART_LEN - 2) / CHARS_PER_ENTRY + 1;

    static uint8_t checksum(const FatDirEntry& entry) {
        uint8_t sum = 0;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&entry);
        for (size_t i = 0; i < sizeof(entry.name) + sizeof(entry.ext); ++i)
            sum = static_cast<uint8_t>(((sum & 1U) << 7) + (sum >> 1) + bytes[i]);
        return sum;
    }

    void reset() { next_ = length_ = 0; }

    sys::array<char, MAX_PART_LEN> name_{};
    unsigned int next_{};
    unsigned int length_{};
    uint8_t checksum_{};
    bool present_{};
};

}  // namespace

Result<int> FatInfo::read_file(FatDirEntry* entry, uint8_t* io_buf, uint32_t offset, uint32_t size) {
    ENSURE(entry && (io_buf || size == 0) && size <= 0x7FFFFFFFU);
    ENSURE(!entry->is_directory());
    if (offset >= entry->file_size || size == 0) {
        return 0;
    }

    uint32_t max_size = entry->file_size - offset;
    if (size > max_size) {
        size = max_size;
    }

    uint32_t cluster = entry->cluster();
    ENSURE(cluster >= 2 && cluster < cluster_count_ + 2);

    auto scratch = TRY(KernelBuffer::alloc(bytes_per_cluster_));
    uint8_t* cluster_buf = scratch.data();
    uint32_t solve_bytes{};

    ClusterChain chain(*this, cluster);
    while (solve_bytes < size && (cluster = TRY(chain.next())) != 0) {
        uint32_t sector = cluster_to_sector(cluster);
        TRY(dev_->read(partition_start_lba_ + sector, cluster_buf, sectors_per_cluster_));

        if (offset >= bytes_per_cluster_) {
            offset -= bytes_per_cluster_;
            continue;
        }

        uint32_t cluster_offset = offset;
        uint32_t cluster_bytes = bytes_per_cluster_ - offset;
        offset = 0;

        uint32_t count = sys::min(cluster_bytes, size - solve_bytes);
        memcpy(io_buf + solve_bytes, cluster_buf + cluster_offset, count);

        solve_bytes += count;
    }

    ENSURE(solve_bytes == size, Error::BadFs);
    return static_cast<int>(solve_bytes);
}

Result<int> FatInfo::read_dir(uint32_t start_cluster, DirVisitor& visitor) {
    int count{};
    auto scratch = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    auto& sector_buf = *reinterpret_cast<SectorArray<FatDirEntry>*>(scratch.data());

    LongName long_name;
    ClusterChain chain(*this, start_cluster);
    uint32_t cluster{};
    while ((cluster = TRY(chain.next())) != 0) {
        uint32_t base_sector = cluster_to_sector(cluster);

        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            uint32_t sector = base_sector + i;
            TRY(dev_->read(partition_start_lba_ + sector, &sector_buf, 1));

            for (auto& entry : sector_buf.entries) {
                if (entry.is_end()) {
                    return count;
                }

                if (!entry.is_valid()) {
                    long_name.consume(entry);
                    continue;
                }

                EntryLocation location{base_sector + i, static_cast<uint32_t>(&entry - sector_buf.entries)};
                if (visitor.visit({entry, location, long_name.name(entry), long_name.present()}) != 0) {
                    return count;
                }

                long_name.clear();
                count++;
            }
        }
    }

    return count;
}

Result<int> FatInfo::read_dir(const char* relpath, DirVisitor& visitor) {
    ENSURE(relpath);

    if (relpath[0] == '\0') {
        return read_dir(root_cluster_, visitor);
    }

    FatDirEntry dir{};
    TRY(find_file(relpath, &dir));
    ENSURE(dir.attr & FAT_ATTR_DIRECTORY);

    uint32_t start_cluster = dir.cluster();
    return read_dir(start_cluster, visitor);
}

Error FatInfo::find_entry(uint32_t start_cluster, const char* name, FatDirEntry* out, EntryLocation* location,
                          bool* has_long_name) {
    class Finder : public DirVisitor {
    public:
        Finder(const char* name, FatDirEntry* out, EntryLocation* location, bool* has_long_name)
            : name_(name), out_(out), location_(location), has_long_name_(has_long_name) {}

        int visit(const DirectoryRecord& record) override {
            if (!short_name_matches(record.entry, name_) &&
                !(record.long_name && name_matches(record.long_name, name_)))
                return 0;
            *out_ = record.entry;
            if (location_)
                *location_ = record.location;
            if (has_long_name_)
                *has_long_name_ = record.has_long_name;
            found_ = true;
            return 1;
        }

        bool found() const { return found_; }

    private:
        const char* name_;
        FatDirEntry* out_;
        EntryLocation* location_;
        bool* has_long_name_;
        bool found_{};
    };

    Finder finder(name, out, location, has_long_name);
    static_cast<void>(TRY(read_dir(start_cluster, finder)));
    return finder.found() ? Error::None : Error::NotFound;
}

Error FatInfo::find_file(const char* filename, FatDirEntry* result, EntryLocation* location) {
    ENSURE(filename && result);

    PathParts parts;
    TRY(split_path(filename, parts));
    uint32_t cluster = root_cluster_;
    for (size_t i = 0; i < parts.size(); i++, cluster = result->cluster()) {
        const char* part = parts[i];
        sys::array<char, MAX_PART_LEN> name{};
        ENSURE(next_part(part, name));
        TRY(find_entry(cluster, name.data(), result, i + 1 == parts.size() ? location : nullptr));

        if (i + 1 < parts.size() && !result->is_directory())
            return Error::NotFound;
    }

    return Error::None;
}

Error FatInfo::read_file_entry(const EntryLocation& location, FatDirEntry* entry) {
    ENSURE(entry && location.index < SectorArray<FatDirEntry>::COUNT, Error::Invalid);
    ENSURE(location.sector >= data_start_sector_ && location.sector < total_sector_count_, Error::Invalid);
    auto storage = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    auto* sector = reinterpret_cast<SectorArray<FatDirEntry>*>(storage.data());
    TRY(dev_->read(partition_start_lba_ + location.sector, sector, 1));
    *entry = sector->entries[location.index];
    ENSURE(entry->is_valid(), Error::NotFound);
    ENSURE(!entry->is_directory(), Error::Invalid);
    return Error::None;
}

Error FatInfo::write_file_entry(const EntryLocation& location, const FatDirEntry& entry) {
    auto storage = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    auto* sector = reinterpret_cast<SectorArray<FatDirEntry>*>(storage.data());
    TRY(dev_->read(partition_start_lba_ + location.sector, sector, 1));
    sector->entries[location.index] = entry;
    return write_sectors(partition_start_lba_ + location.sector, sector, 1);
}

Error FatInfo::validate_file_chain(const FatDirEntry& entry) {
    if (entry.cluster() == 0) {
        ENSURE(entry.file_size == 0, Error::BadFs);
        return Error::None;
    }
    ClusterChain chain(*this, entry.cluster());
    uint32_t count{};
    while (TRY(chain.next()) != 0) {
        ++count;
    }
    ENSURE(static_cast<uint64_t>(count) * bytes_per_cluster_ >= entry.file_size, Error::BadFs);
    return Error::None;
}

Error FatInfo::link_file_cluster(uint32_t tail, uint32_t old_link, uint32_t cluster) {
    if (tail != 0) {
        Error error = write_entry(tail, cluster);
        if (error != Error::None) {
            // Never free a target until every FAT copy no longer references it.
            Error cleanup = write_entry(tail, old_link);
            if (cleanup == Error::None) {
                rollback_new_cluster(cluster);
            } else {
                klog::error("fat: retaining cluster %u after failed tail restore: %s\n", cluster, error_str(cleanup));
            }
            return error;
        }
    }
    return Error::None;
}

Result<int> FatInfo::write_file(const EntryLocation& location, const uint8_t* buf, uint32_t offset, uint32_t size,
                                bool append, size_t* end_offset) {
    ENSURE((buf || size == 0) && size <= 0x7FFFFFFFU, Error::Invalid);
    TRY(free_detached_chain());
    FatDirEntry entry{};
    TRY(read_file_entry(location, &entry));
    ENSURE((entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);
    if (append) {
        offset = entry.file_size;
    }
    ENSURE(size <= 0xFFFFFFFFU - offset, Error::Invalid);
    if (size == 0) {
        if (end_offset) {
            *end_offset = offset;
        }
        return 0;
    }
    TRY(validate_file_chain(entry));

    sys::unique_ptr<WriteState> state(new (sys::nothrow) WriteState());
    ENSURE(state, Error::NoMem);
    state->location = location;
    state->entry = entry;
    state->bytes = buf;
    state->offset = offset;
    state->end = offset + size;
    // Keep confirmed progress when a later sector, allocation or metadata write fails.
    Error error = transfer_file(*state);
    if (error != Error::None && state->done == 0) {
        return error;
    }
    if (end_offset) {
        *end_offset = static_cast<size_t>(offset) + state->done;
    }
    return static_cast<int>(state->done);
}

Error FatInfo::transfer_file(WriteState& state) {
    state.position = sys::min(state.offset, state.entry.file_size);
    state.cluster = state.entry.cluster();
    uint32_t skip = state.position / bytes_per_cluster_;
    while (skip != 0) {
        ENSURE(valid_cluster(state.cluster), Error::BadFs);
        state.tail = state.cluster;
        state.cluster = TRY(read_entry(state.cluster));
        --skip;
    }
    state.cluster_offset = state.position % bytes_per_cluster_;
    while (state.position < state.end) {
        if (!valid_cluster(state.cluster)) {
            uint32_t old_link = fat::FAT32_EOC_MAX;
            if (state.tail != 0) {
                old_link = TRY(read_entry(state.tail));
                ENSURE(old_link >= fat::FAT32_EOC_MIN, Error::BadFs);
            }
            state.cluster = TRY(alloc_cluster());
            TRY(link_file_cluster(state.tail, old_link, state.cluster));
            if (state.tail == 0) {
                state.entry.first_cluster_high = static_cast<uint16_t>(state.cluster >> 16);
                state.entry.first_cluster_low = static_cast<uint16_t>(state.cluster);
                // A failed directory write may already have published this cluster.
                TRY(write_file_entry(state.location, state.entry));
            }
        }
        uint32_t sector_offset = state.cluster_offset % bytes_per_sector_;
        uint32_t count = sys::min(bytes_per_sector_ - sector_offset, state.end - state.position);
        const bool gap = state.position < state.offset;
        if (gap) {
            count = sys::min(count, state.offset - state.position);
        }
        uint32_t lba =
            partition_start_lba_ + cluster_to_sector(state.cluster) + state.cluster_offset / bytes_per_sector_;
        if (sector_offset != 0 || count != bytes_per_sector_) {
            TRY(dev_->read(lba, state.sector.data(), 1));
        }
        if (gap) {
            memset(state.sector.data() + sector_offset, 0, count);
        } else {
            memcpy(state.sector.data() + sector_offset, state.bytes + state.done, count);
        }
        TRY(write_sectors(lba, state.sector.data(), 1));
        FatDirEntry updated = state.entry;
        updated.file_size = sys::max(state.entry.file_size, state.position + count);
        updated.attr |= FAT_ATTR_ARCHIVE;
        if (updated.file_size != state.entry.file_size || updated.attr != state.entry.attr) {
            TRY(write_file_entry(state.location, updated));
            state.entry = updated;
        }
        state.position += count;
        state.cluster_offset += count;
        if (!gap) {
            state.done += count;
        }
        if (state.position < state.end && state.cluster_offset == bytes_per_cluster_) {
            state.tail = state.cluster;
            state.cluster = TRY(read_entry(state.cluster));
            state.cluster_offset = 0;
        }
    }
    return Error::None;
}

Error FatInfo::truncate_file(const EntryLocation& location) {
    TRY(free_detached_chain());
    FatDirEntry entry{};
    TRY(read_file_entry(location, &entry));
    ENSURE((entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);
    TRY(validate_file_chain(entry));
    const uint32_t cluster = entry.cluster();
    entry.first_cluster_high = 0;
    entry.first_cluster_low = 0;
    entry.file_size = 0;
    entry.attr |= FAT_ATTR_ARCHIVE;
    // Detach first. On error retain the chain, since the write may be partial.
    TRY(write_file_entry(location, entry));
    pending_free_cluster_ = cluster;
    return free_detached_chain();
}

Error FatInfo::make_83_name(const char* name, char out_name[8], char out_ext[3]) {
    memset(out_name, ' ', 8);
    memset(out_ext, ' ', 3);

    size_t length = strlen(name);
    ENSURE(length != 0 && name[0] != '.', Error::Invalid);
    bool extension{};
    size_t index{};
    for (size_t i = 0; i < length; ++i) {
        char ch = to_upper(name[i]);
        if (ch == '.') {
            ENSURE(!extension && i + 1 < length, Error::Invalid);
            extension = true;
            index = 0;
            continue;
        }
        ENSURE((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || strchr("$%'-_@~`!(){}^#&", ch), Error::Invalid);
        ENSURE(index < (extension ? 3U : 8U), Error::Invalid);
        (extension ? out_ext : out_name)[index++] = ch;
    }
    return Error::None;
}

Error FatInfo::resolve_parent(const char* relpath, uint32_t* parent_cluster, char* child_name, size_t name_size) {
    ENSURE(relpath && parent_cluster && child_name && name_size > 0);

    const char* last = relpath;
    for (const char* p = relpath; *p; ++p) {
        if (*p == '/') {
            last = p + 1;
        }
    }
    ENSURE(*last && strcmp(last, ".") != 0 && strcmp(last, "..") != 0, Error::Invalid);

    PathParts parts;
    TRY(split_path(relpath, parts));
    sys::array<char, MAX_PART_LEN> name{};
    const char* leaf = parts.back();
    ENSURE(next_part(leaf, name));
    size_t length = strlen(name.data());
    ENSURE(length < name_size);
    memcpy(child_name, name.data(), length + 1);

    uint32_t cluster = root_cluster_;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        const char* part = parts[i];
        ENSURE(next_part(part, name));
        FatDirEntry entry{};
        TRY(find_entry(cluster, name.data(), &entry));
        ENSURE(entry.is_directory(), Error::NotFound);

        cluster = entry.cluster();
    }

    *parent_cluster = cluster;
    return Error::None;
}

void FatInfo::rollback_new_cluster(uint32_t start_cluster) {
    // This is one newly allocated, unpublished cluster, not an arbitrary chain.
    // Finish an uncertain mirrored update before replacing its cached EOC link.
    Error cleanup = flush_fat_buffer();
    if (cleanup == Error::None) {
        cleanup = write_entry(start_cluster, fat::FAT32_FREE);
    }
    if (cleanup != Error::None) {
        cprintf("fat: failed to roll back new cluster %d: %s (%d)\n", start_cluster, error_str(cleanup),
                static_cast<int>(cleanup));
    }
}

Error FatInfo::add_dir_entry(uint32_t dir_cluster, const FatDirEntry* new_entry, bool* entry_may_exist) {
    if (entry_may_exist) {
        *entry_may_exist = false;
    }
    using Sector = SectorArray<FatDirEntry>;
    auto storage = TRY(KernelBuffer::alloc(sizeof(Sector)));
    auto& sector_buf = *new (storage.data()) Sector{};

    ClusterChain chain(*this, dir_cluster);
    uint32_t cluster{};
    while ((cluster = TRY(chain.next())) != 0) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            uint32_t abs_sector = partition_start_lba_ + base_sector + i;
            TRY(dev_->read(abs_sector, &sector_buf, 1));

            for (int j = 0; j < Sector::COUNT; j++) {
                if (sector_buf.entries[j].is_end() || sector_buf.entries[j].is_deleted()) {
                    bool was_end = sector_buf.entries[j].is_end();
                    sector_buf.entries[j] = *new_entry;
                    if (was_end && j + 1 < Sector::COUNT) {
                        sector_buf.entries[j + 1] = {};
                    } else if (was_end) {
                        uint32_t next_sector{};
                        if (i + 1 < sectors_per_cluster_) {
                            next_sector = abs_sector + 1;
                        } else {
                            uint32_t next = TRY(read_entry(cluster));
                            if (next < fat::FAT32_EOC_MIN) {
                                ENSURE(valid_cluster(next), Error::BadFs);
                                next_sector = partition_start_lba_ + cluster_to_sector(next);
                            }
                        }
                        if (next_sector != 0) {
                            // Prepare the new terminator before publishing over the old one.
                            TRY(dev_->read(next_sector, &sector_buf, 1));
                            sector_buf.entries[0] = {};
                            TRY(write_sectors(next_sector, &sector_buf, 1));
                            TRY(dev_->read(abs_sector, &sector_buf, 1));
                            sector_buf.entries[j] = *new_entry;
                        }
                    }
                    // Even an error can follow a partial device write.
                    if (entry_may_exist) {
                        *entry_may_exist = true;
                    }
                    TRY(write_sectors(abs_sector, &sector_buf, 1));
                    return Error::None;
                }
            }
        }

        uint32_t next = TRY(read_entry(cluster));
        if (next >= fat::FAT32_EOC_MIN) {
            uint32_t new_cluster = TRY(alloc_cluster());

            // Initialize the extension before publishing it in the live chain.
            uint32_t new_base_sector = partition_start_lba_ + cluster_to_sector(new_cluster);
            memset(&sector_buf, 0, sizeof(sector_buf));
            sector_buf.entries[0] = *new_entry;

            for (uint32_t s = 0; s < sectors_per_cluster_; s++) {
                Error error = write_sectors(new_base_sector + s, &sector_buf, 1);
                if (error != Error::None) {
                    rollback_new_cluster(new_cluster);
                    return error;
                }
                if (s == 0) {
                    memset(&sector_buf, 0, sizeof(sector_buf));
                }
            }
            Error error = write_entry(cluster, new_cluster);
            if (error != Error::None) {
                // A failed write may have changed the cache or one FAT copy.
                // Restore the old tail before freeing its possible target.
                Error cleanup = write_entry(cluster, next);
                if (cleanup == Error::None) {
                    rollback_new_cluster(new_cluster);
                } else {
                    if (entry_may_exist) {
                        *entry_may_exist = true;
                    }
                    cprintf("fat: failed to restore directory tail %d; retaining cluster %d: %s (%d)\n", cluster,
                            new_cluster, error_str(cleanup), static_cast<int>(cleanup));
                }
                return error;
            }
            return Error::None;
        }
    }

    return Error::Full;
}

Error FatInfo::remove_dir_entry(uint32_t dir_cluster, const char* name) {
    using Sector = SectorArray<FatDirEntry>;
    auto scratch = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    auto& sector_buf = *reinterpret_cast<Sector*>(scratch.data());

    ClusterChain chain(*this, dir_cluster);
    uint32_t cluster{};
    while ((cluster = TRY(chain.next())) != 0) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            uint32_t abs_sector = partition_start_lba_ + base_sector + i;
            TRY(dev_->read(abs_sector, &sector_buf, 1));

            auto& entries = sector_buf.entries;
            for (uint32_t j = 0; j < Sector::COUNT; j++) {
                if (entries[j].is_end()) {
                    return Error::NotFound;
                }

                if (!entries[j].is_valid()) {
                    continue;
                }

                if (short_name_matches(entries[j], name)) {
                    entries[j].name[0] = static_cast<char>(0xE5);  // Mark deleted.
                    TRY(write_sectors(abs_sector, &sector_buf, 1));
                    return Error::None;
                }
            }
        }
    }

    return Error::NotFound;
}

Error FatInfo::mkdir(const char* relpath) {
    ENSURE(relpath);
    TRY(free_detached_chain());

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry existing{};
    Error err = find_entry(parent_cluster, child_name, &existing);
    if (err == Error::None)
        return Error::Exists;
    if (err != Error::NotFound)
        return err;

    FatDirEntry dir_entry{};
    memset(&dir_entry, 0, sizeof(dir_entry));
    TRY(make_83_name(child_name, dir_entry.name, dir_entry.ext));
    auto scratch = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    uint32_t new_cluster = TRY(alloc_cluster());
    dir_entry.attr = FAT_ATTR_DIRECTORY;
    dir_entry.first_cluster_high = static_cast<uint16_t>(new_cluster >> 16);
    dir_entry.first_cluster_low = static_cast<uint16_t>(new_cluster & 0xFFFF);
    dir_entry.file_size = 0;

    // Write the "." and ".." entries into the new directory cluster.
    uint8_t* sector_buf = scratch.data();
    memset(sector_buf, 0, scratch.size());
    auto* entries = reinterpret_cast<FatDirEntry*>(sector_buf);

    // "." entry
    memset(&entries[0], 0, sizeof(FatDirEntry));
    memset(entries[0].name, ' ', 8);
    memset(entries[0].ext, ' ', 3);
    entries[0].name[0] = '.';
    entries[0].attr = FAT_ATTR_DIRECTORY;
    entries[0].first_cluster_high = static_cast<uint16_t>(new_cluster >> 16);
    entries[0].first_cluster_low = static_cast<uint16_t>(new_cluster & 0xFFFF);

    // ".." entry
    memset(&entries[1], 0, sizeof(FatDirEntry));
    memset(entries[1].name, ' ', 8);
    memset(entries[1].ext, ' ', 3);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT_ATTR_DIRECTORY;
    uint32_t parent_val = (parent_cluster == root_cluster_) ? 0 : parent_cluster;
    entries[1].first_cluster_high = static_cast<uint16_t>(parent_val >> 16);
    entries[1].first_cluster_low = static_cast<uint16_t>(parent_val & 0xFFFF);

    uint32_t sector = partition_start_lba_ + cluster_to_sector(new_cluster);
    err = write_sectors(sector, sector_buf, 1);
    if (err != Error::None) {
        rollback_new_cluster(new_cluster);
        return err;
    }

    // Add the entry to the parent directory.
    bool entry_may_exist{};
    err = add_dir_entry(parent_cluster, &dir_entry, &entry_may_exist);
    if (err != Error::None) {
        if (!entry_may_exist) {
            rollback_new_cluster(new_cluster);
        } else {
            // Do not free a cluster that a partially written entry may reference.
            cprintf("fat: mkdir '%s' may have published cluster %d; retaining it after %s (%d)\n", relpath, new_cluster,
                    error_str(err), static_cast<int>(err));
        }
        return err;
    }

    return Error::None;
}

Error FatInfo::create_file(const char* relpath) {
    ENSURE(relpath);
    TRY(free_detached_chain());

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry existing{};
    Error err = find_entry(parent_cluster, child_name, &existing);
    if (err == Error::None)
        return Error::Exists;
    if (err != Error::NotFound)
        return err;

    FatDirEntry file_entry{};
    memset(&file_entry, 0, sizeof(file_entry));
    TRY(make_83_name(child_name, file_entry.name, file_entry.ext));
    file_entry.attr = FAT_ATTR_ARCHIVE;
    file_entry.first_cluster_high = 0;
    file_entry.first_cluster_low = 0;
    file_entry.file_size = 0;

    return add_dir_entry(parent_cluster, &file_entry);
}

Error FatInfo::unlink(const char* relpath) {
    ENSURE(relpath);
    TRY(free_detached_chain());

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry entry{};
    TRY(find_entry(parent_cluster, child_name, &entry));
    // Removing VFAT name records is outside the short-name mutation path.
    ENSURE(short_name_matches(entry, child_name), Error::NotSupported);
    ENSURE(!entry.is_directory());  // Use rmdir for directories.
    ENSURE((entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);
    TRY(validate_file_chain(entry));

    // Detach before freeing; a failed directory write may still reference the chain.
    TRY(remove_dir_entry(parent_cluster, child_name));
    pending_free_cluster_ = entry.cluster();
    return free_detached_chain();
}

Error FatInfo::rename(const char* relpath, const char* new_name) {
    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry entry{};
    EntryLocation location{};
    bool has_long_name{};
    TRY(find_entry(parent_cluster, child_name, &entry, &location, &has_long_name));
    FatDirEntry updated = entry;
    TRY(make_83_name(new_name, updated.name, updated.ext));
    char target[SHORT_NAME_CAPACITY]{};
    updated.get_filename(target, sizeof(target));
    if (strcmp(child_name, target) == 0)
        return Error::None;
    ENSURE(!has_long_name, Error::NotSupported);
    ENSURE((entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);
    Error existing = find_entry(parent_cluster, target, &entry);
    ENSURE(existing != Error::None, Error::Exists);
    ENSURE(existing == Error::NotFound, existing);

    // Keep the directory slot identity, attributes and cluster chain.
    return write_file_entry(location, updated);
}

Error FatInfo::rmdir(const char* relpath) {
    ENSURE(relpath);
    TRY(free_detached_chain());

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry entry{};
    TRY(find_entry(parent_cluster, child_name, &entry));
    // Removing VFAT name records is outside the short-name mutation path.
    ENSURE(short_name_matches(entry, child_name), Error::NotSupported);
    ENSURE(entry.is_directory());  // Not a directory.
    ENSURE((entry.attr & FAT_ATTR_READ_ONLY) == 0, Error::Access);

    // Check that directory is empty (only . and .. allowed).
    uint32_t dir_cluster = entry.cluster();
    auto scratch = TRY(KernelBuffer::alloc(BlockDevice::BLOCK_SIZE_BYTES));
    uint8_t* sector_buf = scratch.data();
    bool empty = true;

    ClusterChain chain(*this, dir_cluster);
    uint32_t cluster{};
    while (empty && (cluster = TRY(chain.next())) != 0) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_ && empty; i++) {
            TRY(dev_->read(partition_start_lba_ + base_sector + i, sector_buf, 1));
            auto* dir_entries = reinterpret_cast<FatDirEntry*>(sector_buf);
            for (uint32_t j = 0; j < bytes_per_sector_ / 32; j++) {
                if (dir_entries[j].is_end()) {
                    break;
                }
                if (!dir_entries[j].is_valid()) {
                    continue;
                }
                // Skip "." and ".." entries.
                if (dir_entries[j].name[0] == '.' && dir_entries[j].name[1] == ' ') {
                    continue;
                }
                if (dir_entries[j].name[0] == '.' && dir_entries[j].name[1] == '.' && dir_entries[j].name[2] == ' ') {
                    continue;
                }
                empty = false;
            }
        }
    }

    ENSURE(empty, Error::NotEmpty);

    TRY(remove_dir_entry(parent_cluster, child_name));
    pending_free_cluster_ = dir_cluster;
    return free_detached_chain();
}
