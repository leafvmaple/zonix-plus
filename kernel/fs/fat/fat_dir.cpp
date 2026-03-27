#include "fs/fat.h"

#include "lib/memory.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "debug/assert.h"

#include <base/bpb.h>

namespace {
constexpr uint32_t FAT_IO_MAX_CLUSTER_BUF = 4096;
}

int FatInfo::is_valid(FatDirEntry* entry, const void* buf, uint32_t offset, uint32_t* size, uint32_t* start_cluster,
                      const char* op) const {
    if (!entry || !buf || !size || !start_cluster || !op) {
        return -1;
    }

    if (entry->attr & FAT_ATTR_DIRECTORY) {
        cprintf("fat_%s_file: cannot %s directory\n", op, op);
        return -1;
    }

    if (offset >= entry->file_size) {
        return 0;
    }

    uint32_t max_size = entry->file_size - offset;
    if (*size > max_size) {
        *size = max_size;
    }

    *start_cluster = get_cluster(*entry);
    if (*start_cluster < 2) {
        cprintf("fat_%s_file: invalid cluster: %d\n", op, *start_cluster);
        return -1;
    }

    if (bytes_per_cluster_ > FAT_IO_MAX_CLUSTER_BUF) {
        cprintf("fat_%s_file: cluster too large\n", op);
        return -1;
    }

    return 1;
}

int FatInfo::file_io_common(FatDirEntry* entry, uint8_t* io_buf, uint32_t offset, uint32_t size, const char* op,
                            bool writeback) {
    uint32_t cluster = 0;
    int rc = is_valid(entry, io_buf, offset, &size, &cluster, op);
    if (rc <= 0) {
        return rc;
    }

    uint8_t cluster_buf[FAT_IO_MAX_CLUSTER_BUF]{};
    uint32_t done{};
    uint32_t skip_bytes = offset;

    while (cluster >= 2 && cluster < fat::FAT32_EOC_MIN && done < size) {
        uint32_t sector = cluster_to_sector(cluster);
        if (dev_->read(partition_start_ + sector, cluster_buf, sectors_per_cluster_) != 0) {
            cprintf("fat_%s_file: failed to read cluster %d\n", op, cluster);
            return -1;
        }

        uint32_t cluster_offset = 0;
        uint32_t cluster_bytes = bytes_per_cluster_;

        if (skip_bytes > 0) {
            if (skip_bytes >= cluster_bytes) {
                skip_bytes -= cluster_bytes;
                cluster = read_entry(cluster);
                continue;
            }
            cluster_offset = skip_bytes;
            cluster_bytes -= skip_bytes;
            skip_bytes = 0;
        }

        uint32_t to_copy = cluster_bytes;
        if (to_copy > size - done) {
            to_copy = size - done;
        }

        if (writeback) {
            memcpy(cluster_buf + cluster_offset, io_buf + done, to_copy);
            if (dev_->write(partition_start_ + sector, cluster_buf, sectors_per_cluster_) != 0) {
                cprintf("fat_%s_file: failed to write cluster %d\n", op, cluster);
                return -1;
            }
        } else {
            memcpy(io_buf + done, cluster_buf + cluster_offset, to_copy);
        }

        done += to_copy;
        cluster = read_entry(cluster);
    }

    return static_cast<int>(done);
}

int FatInfo::read_dir(uint32_t start_cluster, fnCallback callback, void* arg, bool verbose_read_error) {
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

                if (entry.name[0] == 0x00) {
                    return count;
                }

                if (!entry.is_valid()) {
                    continue;
                }

                if (callback(&entry, arg) != 0) {
                    return count;
                }

                count++;
            }
        }
    }

    return count;
}

int FatInfo::read_root_dir(fnCallback callback, void* arg) {
    assert(fat_type_ == fat::TYPE_FAT32);

    if (!callback) {
        return -1;
    }

    return read_dir(root_cluster_, callback, arg, true);
}

int FatInfo::read_dir(const char* relpath, fnCallback callback, void* arg) {
    if (!relpath || !callback || fat_type_ != fat::TYPE_FAT32) {
        return -1;
    }

    if (relpath[0] == '\0') {
        return read_root_dir(callback, arg);
    }

    FatDirEntry dir{};
    if (find_file(relpath, &dir) != 0) {
        return -1;
    }

    if ((dir.attr & FAT_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    uint32_t start_cluster = get_cluster(dir);
    return read_dir(start_cluster, callback, arg, false);
}

int FatInfo::find_file(const char* filename, FatDirEntry* result) {
    assert(fat_type_ == fat::TYPE_FAT32);

    if (!filename || !result) {
        return -1;
    }

    const char* path = str_skip_char(filename, '/');
    if (!path || path[0] == '\0') {
        return -1;
    }

    char component[13]{};
    uint32_t current_cluster = root_cluster_;

    while (*path) {
        size_t comp_len = 0;
        while (*path && *path != '/') {
            if (comp_len + 1 >= sizeof(component)) {
                return -1;
            }
            char ch = *path;
            if (ch >= 'a' && ch <= 'z') {
                ch = static_cast<char>(ch - 32);
            }
            component[comp_len++] = ch;
            path++;
        }
        component[comp_len] = '\0';

        while (*path == '/') {
            path++;
        }

        if (component[0] == '\0' || (component[0] == '.' && component[1] == '\0')) {
            continue;
        }

        if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
            current_cluster = root_cluster_;
            continue;
        }

        bool found = false;
        bool dir_end = false;
        uint8_t sector_buf[512]{};

        for (uint32_t cluster = current_cluster; cluster >= 2 && cluster < fat::FAT32_EOC_MIN;
             cluster = read_entry(cluster)) {
            uint32_t base_sector = cluster_to_sector(cluster);

            for (uint32_t i = 0; i < sectors_per_cluster_; i++) {
                uint32_t sector = base_sector + i;
                if (dev_->read(partition_start_ + sector, sector_buf, 1) != 0) {
                    return -1;
                }

                auto* entries = reinterpret_cast<FatDirEntry*>(sector_buf);
                for (uint32_t j = 0; j < bytes_per_sector_ / 32; j++) {
                    FatDirEntry& entry = entries[j];

                    if (entry.name[0] == 0x00) {
                        dir_end = true;
                        break;
                    }

                    if (!entry.is_valid()) {
                        continue;
                    }

                    char entry_name[13]{};
                    get_filename(&entry, entry_name, sizeof(entry_name));
                    for (size_t k = 0; entry_name[k] != '\0'; k++) {
                        if (entry_name[k] >= 'a' && entry_name[k] <= 'z') {
                            entry_name[k] = static_cast<char>(entry_name[k] - 32);
                        }
                    }

                    if (strcmp(entry_name, component) == 0) {
                        *result = entry;
                        current_cluster = get_cluster(*result);
                        found = true;
                        break;
                    }
                }

                if (found || dir_end) {
                    break;
                }
            }

            if (found || dir_end) {
                break;
            }
        }

        if (!found) {
            return -1;
        }

        if (*path != '\0' && (result->attr & FAT_ATTR_DIRECTORY) == 0) {
            return -1;
        }
    }

    return 0;
}

int FatInfo::read_file(FatDirEntry* entry, uint8_t* buf, uint32_t offset, uint32_t size) {
    return file_io_common(entry, buf, offset, size, "read", false);
}

int FatInfo::write_file(FatDirEntry* entry, const uint8_t* buf, uint32_t offset, uint32_t size) {
    return file_io_common(entry, const_cast<uint8_t*>(buf), offset, size, "write", true);
}
