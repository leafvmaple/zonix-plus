#include "fs/fat.h"

#include <sys/inplace_vector.hpp>
#include <sys/array.hpp>
#include <sys/algorithm.hpp>
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/kernel_buffer.h"
#include <base/bpb.h>

namespace {

constexpr int MAX_PART_LEN = 13;
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

}  // namespace

Result<int> FatInfo::do_file_io(FatDirEntry* entry, uint8_t* io_buf, uint32_t offset, uint32_t size, bool writeback) {
    ENSURE(entry && io_buf);
    ENSURE(!entry->is_directory());
    ENSURE(offset <= entry->file_size);
    if (offset == entry->file_size || size == 0) {
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
        if (writeback) {
            memcpy(cluster_buf + cluster_offset, io_buf + solve_bytes, count);
            TRY(dev_->write(partition_start_lba_ + sector, cluster_buf, sectors_per_cluster_));
        } else {
            memcpy(io_buf + solve_bytes, cluster_buf + cluster_offset, count);
        }

        solve_bytes += count;
    }

    ENSURE(solve_bytes == size, Error::BadFs);
    return static_cast<int>(solve_bytes);
}

Result<int> FatInfo::read_dir(uint32_t start_cluster, DirVisitor& visitor) {
    int count{};
    SectorArray<FatDirEntry> sector_buf{};

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
                    continue;
                }

                if (visitor.visit(&entry) != 0) {
                    return count;
                }

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

Error FatInfo::find_entry(uint32_t start_cluster, const char* name, FatDirEntry* out) {
    SectorArray<FatDirEntry> sector_buf{};

    ClusterChain chain(*this, start_cluster);
    uint32_t cluster{};
    while ((cluster = TRY(chain.next())) != 0) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            TRY(dev_->read(partition_start_lba_ + base_sector + i, &sector_buf, 1));

            for (auto& entry : sector_buf.entries) {
                if (entry.is_end())
                    return Error::NotFound;

                if (!entry.is_valid())
                    continue;

                char entry_name[MAX_PART_LEN]{};
                entry.get_filename(entry_name, sizeof(entry_name));
                for (size_t k = 0; entry_name[k]; k++) {
                    entry_name[k] = to_upper(entry_name[k]);
                }

                if (strcmp(entry_name, name) == 0) {
                    *out = entry;
                    return Error::None;
                }
            }
        }
    }

    return Error::NotFound;
}

Error FatInfo::find_file(const char* filename, FatDirEntry* result) {
    ENSURE(filename && result);

    const char* path = str_skip_char(filename, '/');
    ENSURE(path && path[0] != '\0');

    sys::inplace_vector<sys::array<char, MAX_PART_LEN>, MAX_DEPTH> parts{};
    while (*path) {
        sys::array<char, MAX_PART_LEN> part{};
        ENSURE(next_part(path, part));

        if (!strcmp(part.data(), "."))
            continue;

        if (!strcmp(part.data(), "..")) {
            if (!parts.empty())
                parts.pop_back();
            continue;
        }

        ENSURE(parts.try_push_back(part));
    }

    ENSURE(!parts.empty());

    uint32_t cluster = root_cluster_;
    for (size_t i = 0; i < parts.size(); i++, cluster = result->cluster()) {
        TRY(find_entry(cluster, parts[i].data(), result));

        if (i + 1 < parts.size() && !result->is_directory())
            return Error::NotFound;
    }

    return Error::None;
}

Result<int> FatInfo::read_file(FatDirEntry* entry, uint8_t* buf, uint32_t offset, uint32_t size) {
    return do_file_io(entry, buf, offset, size, false);
}

Result<int> FatInfo::write_file(FatDirEntry* entry, const uint8_t* buf, uint32_t offset, uint32_t size) {
    return do_file_io(entry, const_cast<uint8_t*>(buf), offset, size, true);
}

void FatInfo::make_83_name(const char* name, char out_name[8], char out_ext[3]) {
    memset(out_name, ' ', 8);
    memset(out_ext, ' ', 3);

    const char* p = name;
    for (int i = 0; *p && *p != '.' && i < 8;)
        out_name[i++] = to_upper(*p++);

    if (*p == '.')
        p++;

    for (int i = 0; *p && i < 3;)
        out_ext[i++] = to_upper(*p++);
}

Error FatInfo::resolve_parent(const char* relpath, uint32_t* parent_cluster, char* child_name, size_t name_size) {
    ENSURE(relpath && parent_cluster && child_name && name_size > 0);

    const char* path = str_skip_char(relpath, '/');
    ENSURE(path && path[0] != '\0');

    sys::inplace_vector<sys::array<char, MAX_PART_LEN>, MAX_DEPTH> parts{};
    const char* cursor = path;
    while (*cursor) {
        sys::array<char, MAX_PART_LEN> part{};
        ENSURE(next_part(cursor, part));
        if (!strcmp(part.data(), ".")) {
            continue;
        }
        if (!strcmp(part.data(), "..")) {
            if (!parts.empty())
                parts.pop_back();
            continue;
        }
        ENSURE(parts.try_push_back(part));
    }

    ENSURE(!parts.empty());

    strncpy(child_name, parts.back().data(), name_size - 1);
    child_name[name_size - 1] = '\0';

    uint32_t cluster = root_cluster_;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        FatDirEntry entry{};
        TRY(find_entry(cluster, parts[i].data(), &entry));
        ENSURE(entry.is_directory(), Error::NotFound);

        cluster = entry.cluster();
    }

    *parent_cluster = cluster;
    return Error::None;
}

void FatInfo::rollback_new_chain(uint32_t start_cluster) {
    // Cleanup failure is secondary: preserve the operation's original error.
    Error cleanup = free_chain(start_cluster);
    if (cleanup != Error::None) {
        cprintf("fat: failed to roll back new chain %d: %s (%d)\n", start_cluster, error_str(cleanup),
                static_cast<int>(cleanup));
    }
}

Error FatInfo::add_dir_entry(uint32_t dir_cluster, const FatDirEntry* new_entry, bool* entry_may_exist) {
    if (entry_may_exist) {
        *entry_may_exist = false;
    }
    using Sector = SectorArray<FatDirEntry>;
    Sector sector_buf{};

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
                    }
                    // Even an error can follow a partial device write.
                    if (entry_may_exist) {
                        *entry_may_exist = true;
                    }
                    TRY(dev_->write(abs_sector, &sector_buf, 1));
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
                Error error = dev_->write(new_base_sector + s, &sector_buf, 1);
                if (error != Error::None) {
                    rollback_new_chain(new_cluster);
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
                    rollback_new_chain(new_cluster);
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
    Sector sector_buf{};

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

                char entry_name[MAX_PART_LEN]{};
                entries[j].get_filename(entry_name, sizeof(entry_name));
                for (size_t k = 0; entry_name[k]; k++) {
                    entry_name[k] = to_upper(entry_name[k]);
                }

                if (strcmp(entry_name, name) == 0) {
                    entries[j].name[0] = static_cast<char>(0xE5);  // Mark deleted.
                    TRY(dev_->write(abs_sector, &sector_buf, 1));
                    return Error::None;
                }
            }
        }
    }

    return Error::NotFound;
}

Error FatInfo::mkdir(const char* relpath) {
    ENSURE(relpath);

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry existing{};
    Error err = find_entry(parent_cluster, child_name, &existing);
    if (err == Error::None)
        return Error::Exists;
    if (err != Error::NotFound)
        return err;

    uint32_t new_cluster = TRY(alloc_cluster());

    FatDirEntry dir_entry{};
    memset(&dir_entry, 0, sizeof(dir_entry));
    make_83_name(child_name, dir_entry.name, dir_entry.ext);
    dir_entry.attr = FAT_ATTR_DIRECTORY;
    dir_entry.first_cluster_high = static_cast<uint16_t>(new_cluster >> 16);
    dir_entry.first_cluster_low = static_cast<uint16_t>(new_cluster & 0xFFFF);
    dir_entry.file_size = 0;

    // Write the "." and ".." entries into the new directory cluster.
    uint8_t sector_buf[512]{};
    memset(sector_buf, 0, sizeof(sector_buf));
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
    err = dev_->write(sector, sector_buf, 1);
    if (err != Error::None) {
        rollback_new_chain(new_cluster);
        return err;
    }

    // Add the entry to the parent directory.
    bool entry_may_exist{};
    err = add_dir_entry(parent_cluster, &dir_entry, &entry_may_exist);
    if (err != Error::None) {
        if (!entry_may_exist) {
            rollback_new_chain(new_cluster);
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
    make_83_name(child_name, file_entry.name, file_entry.ext);
    file_entry.attr = FAT_ATTR_ARCHIVE;
    file_entry.first_cluster_high = 0;
    file_entry.first_cluster_low = 0;
    file_entry.file_size = 0;

    return add_dir_entry(parent_cluster, &file_entry);
}

Error FatInfo::unlink(const char* relpath) {
    ENSURE(relpath);

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry entry{};
    TRY(find_entry(parent_cluster, child_name, &entry));
    ENSURE(!entry.is_directory());  // Use rmdir for directories.

    // Free the cluster chain if present.
    uint32_t cluster = entry.cluster();
    if (cluster >= 2) {
        TRY(free_chain(cluster));
    }

    return remove_dir_entry(parent_cluster, child_name);
}

Error FatInfo::rmdir(const char* relpath) {
    ENSURE(relpath);

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    TRY(resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)));

    FatDirEntry entry{};
    TRY(find_entry(parent_cluster, child_name, &entry));
    ENSURE(entry.is_directory());  // Not a directory.

    // Check that directory is empty (only . and .. allowed).
    uint32_t dir_cluster = entry.cluster();
    uint8_t sector_buf[512]{};
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

    // Free the directory's cluster chain.
    TRY(free_chain(dir_cluster));

    return remove_dir_entry(parent_cluster, child_name);
}
