#include "fs/fat.h"

#include "lib/array.h"
#include "lib/math.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include <base/bpb.h>

namespace {

constexpr int MAX_PART_LEN = 13;
constexpr int MAX_DEPTH = 16;
constexpr uint32_t FAT_IO_MAX_CLUSTER_BUF = 4096;

static char to_upper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 32) : ch;
}

template<size_t N>
static bool next_part(const char*& path, char (&buf)[N]) {
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

int FatInfo::do_file_io(FatDirEntry* entry, uint8_t* io_buf, uint32_t offset, uint32_t size, const char* op,
                        bool writeback) {
    if (!entry || !io_buf || !op) {
        return -1;
    }

    // TODO
    if (entry->is_directory()) {
        cprintf("fat_%s_file: cannot %s directory\n", op, op);
        return -1;
    }

    if (offset >= entry->file_size) {
        return -1;
    }

    uint32_t max_size = entry->file_size - offset;
    if (size > max_size) {
        size = max_size;
    }

    uint32_t cluster = entry->get_cluster();
    if (cluster < 2) {
        cprintf("fat_%s_file: invalid cluster: %d\n", op, cluster);
        return -1;
    }

    uint8_t cluster_buf[FAT_IO_MAX_CLUSTER_BUF]{};
    uint32_t solve_bytes{};

    for (; cluster >= 2 && cluster < fat::FAT32_EOC_MIN && solve_bytes < size; cluster = read_entry(cluster)) {
        uint32_t sector = cluster_to_sector(cluster);
        if (dev_->read(partition_start_ + sector, cluster_buf, sectors_per_cluster_) != 0) {
            cprintf("fat_%s_file: failed to read cluster %d\n", op, cluster);
            return -1;
        }

        uint32_t cluster_offset = 0;
        uint32_t cluster_bytes = bytes_per_cluster_;

        if (offset > 0) {
            if (offset >= cluster_bytes) {
                offset -= cluster_bytes;
                continue;
            }
            cluster_offset = offset;
            cluster_bytes -= offset;
            offset = 0;
        }

        uint32_t count = min(cluster_bytes, size - solve_bytes);
        if (writeback) {
            memcpy(cluster_buf + cluster_offset, io_buf + solve_bytes, count);
            if (dev_->write(partition_start_ + sector, cluster_buf, sectors_per_cluster_) != 0) {
                cprintf("fat_%s_file: failed to write cluster %d\n", op, cluster);
                return -1;
            }
        } else {
            memcpy(io_buf + solve_bytes, cluster_buf + cluster_offset, count);
        }

        solve_bytes += count;
    }

    return static_cast<int>(solve_bytes);
}

int FatInfo::read_dir(uint32_t start_cluster, DirVisitor& visitor, bool verbose_read_error) {
    int count{};
    SectorArray<FatDirEntry> sector_buf{};

    for (uint32_t cluster = start_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN;
         cluster = read_entry(cluster)) {
        uint32_t base_sector = cluster_to_sector(cluster);

        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            uint32_t sector = base_sector + i;
            if (dev_->read(partition_start_ + sector, &sector_buf, 1) != 0) {
                if (verbose_read_error) {
                    cprintf("fat_read_dir: failed to read sector %d\n", sector);
                }
                return -1;
            }

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

int FatInfo::read_dir(const char* relpath, DirVisitor& visitor) {
    if (!relpath) {
        return -1;
    }

    if (relpath[0] == '\0') {
        return read_dir(root_cluster_, visitor, true);
    }

    FatDirEntry dir{};
    if (find_file(relpath, &dir) != 0) {
        return -1;
    }

    if ((dir.attr & FAT_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    uint32_t start_cluster = dir.get_cluster();
    return read_dir(start_cluster, visitor, false);
}

bool FatInfo::find_entry(uint32_t start_cluster, const char* name, FatDirEntry* out) {
    SectorArray<FatDirEntry> sector_buf{};

    for (uint32_t cluster = start_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN;
         cluster = read_entry(cluster)) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            if (dev_->read(partition_start_ + base_sector + i, &sector_buf, 1) != 0)
                return false;

            for (auto& entry : sector_buf.entries) {
                if (entry.is_end())
                    return false;

                if (!entry.is_valid())
                    continue;

                char entry_name[MAX_PART_LEN]{};
                entry.get_filename(entry_name, sizeof(entry_name));
                for (size_t k = 0; entry_name[k]; k++) {
                    entry_name[k] = to_upper(entry_name[k]);
                }

                if (strcmp(entry_name, name) == 0) {
                    *out = entry;
                    return true;
                }
            }
        }
    }

    return false;
}

int FatInfo::find_file(const char* filename, FatDirEntry* result) {
    if (!filename || !result) {
        return -1;
    }

    const char* path = str_skip_char(filename, '/');
    if (!path || path[0] == '\0') {
        return -1;
    }

    Array<char[MAX_PART_LEN], MAX_DEPTH> parts{};
    while (*path) {
        char part[MAX_PART_LEN]{};
        if (!next_part(path, part))
            return -1;

        if (!strcmp(part, "."))
            continue;

        if (!strcmp(part, "..")) {
            parts.pop_back();
            continue;
        }

        if (!parts.push_back(part))
            return -1;
    }

    if (parts.empty())
        return -1;

    uint32_t cluster = root_cluster_;
    for (size_t i = 0; i < parts.size(); i++, cluster = result->get_cluster()) {
        if (!find_entry(cluster, parts[i], result))
            return -1;

        if (i + 1 < parts.size() && !result->is_directory())
            return -1;
    }

    return 0;
}

int FatInfo::read_file(FatDirEntry* entry, uint8_t* buf, uint32_t offset, uint32_t size) {
    return do_file_io(entry, buf, offset, size, "read", false);
}

int FatInfo::write_file(FatDirEntry* entry, const uint8_t* buf, uint32_t offset, uint32_t size) {
    return do_file_io(entry, const_cast<uint8_t*>(buf), offset, size, "write", true);
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

int FatInfo::resolve_parent(const char* relpath, uint32_t* parent_cluster, char* child_name, size_t name_size) {
    if (!relpath || !parent_cluster || !child_name || name_size == 0) {
        return -1;
    }

    const char* path = str_skip_char(relpath, '/');
    if (!path || path[0] == '\0') {
        return -1;
    }

    Array<char[MAX_PART_LEN], MAX_DEPTH> parts{};
    const char* cursor = path;
    while (*cursor) {
        char part[MAX_PART_LEN]{};
        if (!next_part(cursor, part)) {
            return -1;
        }
        if (!strcmp(part, ".")) {
            continue;
        }
        if (!strcmp(part, "..")) {
            parts.pop_back();
            continue;
        }
        if (!parts.push_back(part)) {
            return -1;
        }
    }

    if (parts.empty()) {
        return -1;
    }

    strncpy(child_name, parts[parts.size() - 1], name_size - 1);
    child_name[name_size - 1] = '\0';

    uint32_t cluster = root_cluster_;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        FatDirEntry entry{};
        if (!find_entry(cluster, parts[i], &entry))
            return -1;

        if (!entry.is_directory())
            return -1;

        cluster = entry.get_cluster();
    }

    *parent_cluster = cluster;
    return 0;
}

int FatInfo::add_dir_entry(uint32_t dir_cluster, const FatDirEntry* new_entry) {
    using Sector = SectorArray<FatDirEntry>;
    Sector sector_buf{};

    for (uint32_t cluster = dir_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN; cluster = read_entry(cluster)) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            uint32_t abs_sector = partition_start_ + base_sector + i;
            if (dev_->read(abs_sector, &sector_buf, 1) != 0) {
                return -1;
            }

            for (int j = 0; j < Sector::COUNT; j++) {
                if (sector_buf.entries[j].is_end() || sector_buf.entries[j].is_deleted()) {
                    bool was_end = sector_buf.entries[j].is_end();
                    sector_buf.entries[j] = *new_entry;
                    if (was_end && j + 1 < Sector::COUNT) {
                        sector_buf.entries[j + 1] = {};
                    }
                    if (dev_->write(abs_sector, &sector_buf, 1) != 0) {
                        return -1;
                    }
                    return 0;
                }
            }
        }

        uint32_t next = read_entry(cluster);
        if (next >= fat::FAT32_EOC_MIN) {
            uint32_t new_cluster = alloc_cluster();
            if (new_cluster == 0)
                return -1;

            if (write_entry(cluster, new_cluster) != 0) {
                free_chain(new_cluster);
                return -1;
            }

            uint32_t new_base_sector = partition_start_ + cluster_to_sector(new_cluster);
            memset(&sector_buf, 0, sizeof(sector_buf));
            sector_buf.entries[0] = *new_entry;

            for (uint32_t s = 0; s < sectors_per_cluster_; s++) {
                if (dev_->write(new_base_sector + s, &sector_buf, 1) != 0) {
                    free_chain(new_cluster);
                    return -1;
                }
                if (s == 0) {
                    memset(&sector_buf, 0, sizeof(sector_buf));
                }
            }
            return 0;
        }
    }

    return -1;
}

int FatInfo::remove_dir_entry(uint32_t dir_cluster, const char* name) {
    using Sector = SectorArray<FatDirEntry>;
    Sector sector_buf{};

    for (uint32_t cluster = dir_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN; cluster = read_entry(cluster)) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            uint32_t abs_sector = partition_start_ + base_sector + i;
            if (dev_->read(abs_sector, &sector_buf, 1) != 0) {
                return -1;
            }

            auto& entries = sector_buf.entries;
            for (uint32_t j = 0; j < Sector::COUNT; j++) {
                if (entries[j].is_end()) {
                    return -1;  // Not found.
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
                    if (dev_->write(abs_sector, &sector_buf, 1) != 0) {
                        return -1;
                    }
                    return 0;
                }
            }
        }
    }

    return -1;
}

int FatInfo::mkdir(const char* relpath) {
    if (!relpath) {
        return -1;
    }

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    if (resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)) != 0) {
        return -1;
    }

    FatDirEntry existing{};
    if (find_entry(parent_cluster, child_name, &existing)) {
        return -1;  // Already exists.
    }

    uint32_t new_cluster = alloc_cluster();
    if (new_cluster == 0) {
        return -1;
    }

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

    uint32_t sector = partition_start_ + cluster_to_sector(new_cluster);
    if (dev_->write(sector, sector_buf, 1) != 0) {
        free_chain(new_cluster);
        return -1;
    }

    // Add the entry to the parent directory.
    if (add_dir_entry(parent_cluster, &dir_entry) != 0) {
        free_chain(new_cluster);
        return -1;
    }

    return 0;
}

int FatInfo::create_file(const char* relpath) {
    if (!relpath) {
        return -1;
    }

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    if (resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)) != 0) {
        return -1;
    }

    FatDirEntry existing{};
    if (find_entry(parent_cluster, child_name, &existing)) {
        return -1;
    }

    FatDirEntry file_entry{};
    memset(&file_entry, 0, sizeof(file_entry));
    make_83_name(child_name, file_entry.name, file_entry.ext);
    file_entry.attr = FAT_ATTR_ARCHIVE;
    file_entry.first_cluster_high = 0;
    file_entry.first_cluster_low = 0;
    file_entry.file_size = 0;

    return add_dir_entry(parent_cluster, &file_entry);
}

int FatInfo::unlink(const char* relpath) {
    if (!relpath) {
        return -1;
    }

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    if (resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)) != 0) {
        return -1;
    }

    FatDirEntry entry{};
    if (!find_entry(parent_cluster, child_name, &entry)) {
        return -1;  // Not found.
    }

    if (entry.is_directory()) {
        return -1;  // Use rmdir for directories.
    }

    // Free the cluster chain if present.
    uint32_t cluster = entry.get_cluster();
    if (cluster >= 2) {
        if (free_chain(cluster) != 0) {
            return -1;
        }
    }

    return remove_dir_entry(parent_cluster, child_name);
}

int FatInfo::rmdir(const char* relpath) {
    if (!relpath) {
        return -1;
    }

    uint32_t parent_cluster{};
    char child_name[MAX_PART_LEN]{};
    if (resolve_parent(relpath, &parent_cluster, child_name, sizeof(child_name)) != 0) {
        return -1;
    }

    FatDirEntry entry{};
    if (!find_entry(parent_cluster, child_name, &entry)) {
        return -1;
    }

    if (!entry.is_directory()) {
        return -1;  // Not a directory.
    }

    // Check that directory is empty (only . and .. allowed).
    uint32_t dir_cluster = entry.get_cluster();
    uint8_t sector_buf[512]{};
    bool empty = true;

    for (uint32_t cluster = dir_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN && empty;
         cluster = read_entry(cluster)) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_ && empty; i++) {
            if (dev_->read(partition_start_ + base_sector + i, sector_buf, 1) != 0) {
                return -1;
            }
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

    if (!empty) {
        cprintf("fat_rmdir: directory not empty\n");
        return -1;
    }

    // Free the directory's cluster chain.
    if (free_chain(dir_cluster) != 0) {
        return -1;
    }

    return remove_dir_entry(parent_cluster, child_name);
}
