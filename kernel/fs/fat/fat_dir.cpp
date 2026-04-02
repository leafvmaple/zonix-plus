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
    if (start_cluster < 2) {
        return -1;
    }

    int count{};
    uint8_t sector_buf[512]{};

    for (uint32_t cluster = start_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN;
         cluster = read_entry(cluster)) {
        uint32_t base_sector = cluster_to_sector(cluster);

        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            uint32_t sector = base_sector + i;
            if (dev_->read(partition_start_ + sector, sector_buf, 1) != 0) {
                if (verbose_read_error) {
                    cprintf("fat_read_dir: failed to read sector %d\n", sector);
                }
                return -1;
            }

            auto* entries = reinterpret_cast<FatDirEntry*>(sector_buf);
            for (uint32_t j = 0; j < bytes_per_sector_ / 32; j++) {
                FatDirEntry& entry = entries[j];

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
    uint8_t sector_buf[512]{};

    for (uint32_t cluster = start_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN;
         cluster = read_entry(cluster)) {
        uint32_t base_sector = cluster_to_sector(cluster);
        for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
            if (dev_->read(partition_start_ + base_sector + i, sector_buf, 1) != 0) {
                return false;
            }

            auto* entries = reinterpret_cast<FatDirEntry*>(sector_buf);
            for (uint32_t j = 0; j < bytes_per_sector_ / 32; j++) {
                FatDirEntry& entry = entries[j];

                if (entry.is_end()) {
                    return false;
                }

                if (!entry.is_valid()) {
                    continue;
                }

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
