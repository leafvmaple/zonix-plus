#pragma once

#include "../command.h"
#include <sys/array.hpp>
#include <sys/string.hpp>
#include <sys/utility.hpp>
#include <sys/vector.hpp>

namespace files {

// The directory being browsed: its path, one page of entries and the history
// of visited paths. It follows the process's working directory and knows
// nothing about how the entries are shown.
class Directory {
public:
    // The number of entries a page holds.
    void set_page_size(int size) {
        page_size_ = size;
        page_.reserve(size);
    }

    const char* path() const { return path_; }
    bool at_root() const { return path_[0] == '/' && path_[1] == 0; }

    int size() const { return static_cast<int>(page_.size()); }
    bool empty() const { return page_.empty(); }
    const DirectoryEntry& entry(int index) const { return page_[index].item; }

    int page_number() const { return page_number_; }
    bool has_previous_page() const { return page_number_ > 1; }
    bool has_next_page() const { return next_first_ > 0; }
    bool can_go_back() const { return history_index_ > 0; }
    bool can_go_forward() const { return history_index_ + 1 < history_count_; }

    // The failure of the most recent operation, or 0.
    long error() const { return error_; }
    // Changes whenever a page of entries is read.
    unsigned int revision() const { return revision_; }

    // Re-reads the working directory from its first page.
    void refresh() {
        error_ = 0;
        first_ = 0;
        page_number_ = 1;
        long result = sys_getcwd(path_, sizeof(path_));
        if (result < 0)
            error_ = result;
        read_page();
        if (!page_.empty())
            first_ = page_[0].index;
    }

    // Enters a directory and adds it to the history.
    void open(const char* path) {
        if (change(path))
            remember();
    }

    // Adds the current path to the history unless it is already the latest entry.
    void remember() {
        if (history_index_ >= 0 && history_[history_index_] == path_)
            return;
        if (history_index_ == HISTORY_SIZE - 1) {
            for (int i = 1; i < HISTORY_SIZE; ++i)
                history_[i - 1] = sys::move(history_[i]);
            --history_index_;
        }
        history_[++history_index_] = path_;
        history_count_ = history_index_ + 1;
    }

    // Moves through the history. Returns false when there is nowhere to go.
    bool step(int delta) {
        int target = history_index_ + delta;
        if (target < 0 || target >= history_count_)
            return false;
        if (change(history_[target].c_str()))
            history_index_ = target;
        return true;
    }

    // Enters the parent directory. Returns false at the root.
    bool up() {
        if (at_root())
            return false;
        open("..");
        return true;
    }

    // Shows the next or previous page. Returns whether the page changed.
    bool turn_page(int delta) {
        error_ = 0;
        if (delta > 0 && next_first_ > 0) {
            first_ = next_first_;
            ++page_number_;
        } else if (delta < 0 && page_number_ > 1) {
            int remaining = page_size_;
            long index = first_;
            while (index > 0 && remaining > 0) {
                DirectoryEntry entry{};
                long result = sys_readdir((char*)".", --index, &entry);
                if (result < 0) {
                    error_ = result;
                    return false;
                }
                if (result > 0 && !dot_entry(entry))
                    --remaining;
            }
            first_ = index;
            --page_number_;
        } else {
            return false;
        }
        read_page();
        return true;
    }

private:
    struct Entry {
        DirectoryEntry item;
        long index;
    };
    static constexpr int HISTORY_SIZE = 16;

    static bool dot_entry(const DirectoryEntry& entry) {
        return entry.name[0] == '.' && (entry.name[1] == 0 || (entry.name[1] == '.' && entry.name[2] == 0));
    }

    bool change(const char* path) {
        long result = sys_chdir(const_cast<char*>(path));
        if (result < 0) {
            error_ = result;
            return false;
        }
        refresh();
        return true;
    }

    void read_page() {
        page_.clear();
        next_first_ = 0;
        ++revision_;
        for (long index = first_;; ++index) {
            DirectoryEntry entry{};
            long result = sys_readdir((char*)".", index, &entry);
            if (result <= 0) {
                if (result < 0)
                    error_ = result;
                break;
            }
            if (dot_entry(entry))
                continue;
            if (static_cast<int>(page_.size()) == page_size_) {
                next_first_ = index;
                break;
            }
            page_.push_back({entry, index});
        }
    }

    sys::vector<Entry> page_;
    sys::array<sys::string, HISTORY_SIZE> history_{};
    char path_[PROCESS_MAX_PATH]{"/"};
    long error_{};
    long first_{};
    long next_first_{};
    unsigned int revision_{};
    int page_size_{};
    int page_number_ = 1;
    int history_count_{};
    int history_index_ = -1;
};

}  // namespace files
