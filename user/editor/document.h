#pragma once

#include "../command.h"
#include <sys/array.hpp>
#include <sys/vector.hpp>

namespace editor {

// The text being edited: its bytes, the cursor and the file it belongs to. It
// accepts the same files as vi (ASCII with LF line ends and tabs, at most
// 64 KiB) and knows nothing about how the text is shown.
class Document {
public:
    static constexpr sys::size_t CAPACITY = 65536;
    static constexpr int TAB_WIDTH = 8;

    // Reads the file. A missing file starts an empty document under that name.
    long load(const char* path) {
        sys::size_t name = strlen(const_cast<char*>(path));
        if (name == 0 || name >= path_.size())
            return SYS_ERROR_INVALID;
        text_.resize(CAPACITY);
        if (text_.size() != CAPACITY)
            return SYS_ERROR_FULL;
        memcpy(path_.data(), path, name + 1);

        FileStat st{};
        long error = sys_stat(path_.data(), &st);
        if (error == SYS_ERROR_NOT_FOUND)
            return 0;
        if (error < 0)
            return error;
        if (st.type != FILE_TYPE_FILE)
            return SYS_ERROR_INVALID;
        if (st.size > CAPACITY)
            return SYS_ERROR_FULL;
        int fd = sys_open(path_.data(), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return fd;
        sys::size_t length = 0;
        while (error == 0) {
            // One byte beyond the limit shows that the file has grown past it.
            char extra;
            bool full = length == CAPACITY;
            long count = sys_read(fd, full ? &extra : text_.data() + length, full ? 1 : CAPACITY - length);
            if (count < 0)
                error = count;
            else if (count == 0)
                break;
            else if (full)
                error = SYS_ERROR_FULL;
            else
                length += static_cast<sys::size_t>(count);
        }
        if (sys_close(fd) < 0 && error == 0)
            error = SYS_ERROR_IO;
        for (sys::size_t i = 0; error == 0 && i < length; ++i) {
            if (!is_text(text_[i]))
                error = SYS_ERROR_NOT_SUPPORTED;
        }
        if (error == 0)
            length_ = length;
        return error;
    }

    // Replaces the file with the text and syncs it. The text stays modified
    // when this fails, and the file may then be incomplete.
    long save() {
        int fd = sys_open(path_.data(), O_WRONLY | O_CREAT | O_CLOEXEC);
        if (fd < 0)
            return fd;
        DescriptorStat st{};
        long error = sys_fstat(fd, &st);
        if (error == 0 && st.type != FILE_TYPE_FILE)
            error = SYS_ERROR_INVALID;
        if (error == 0) {
            modified_ = true;
            error = sys_ftruncate(fd, 0);
        }
        if (error == 0)
            error = command_write_all(fd, text_.data(), static_cast<long>(length_));
        if (sys_close(fd) < 0 && error == 0)
            error = SYS_ERROR_IO;
        if (error == 0)
            error = sys_sync();
        if (error == 0)
            modified_ = false;
        return error;
    }

    const char* path() const { return path_.data(); }
    bool modified() const { return modified_; }
    const char* data() const { return text_.data(); }
    sys::size_t size() const { return length_; }
    sys::size_t cursor() const { return cursor_; }

    static bool is_text(char byte) { return byte == '\n' || byte == '\t' || (byte >= ' ' && byte <= '~'); }

    // The display column after a byte shown at `column`.
    static int advance(int column, char byte) { return byte == '\t' ? (column / TAB_WIDTH + 1) * TAB_WIDTH : column + 1; }

    int line_count() const {
        int lines = 1;
        for (sys::size_t i = 0; i < length_; ++i)
            lines += text_[i] == '\n';
        return lines;
    }

    // The zero-based line and display column of the cursor.
    int line() const {
        int line = 0;
        for (sys::size_t i = 0; i < cursor_; ++i)
            line += text_[i] == '\n';
        return line;
    }
    int column() const {
        int column = 0;
        for (sys::size_t i = line_start(cursor_); i < cursor_; ++i)
            column = advance(column, text_[i]);
        return column;
    }

    // Where a line starts. Lines past the end give the size of the text.
    sys::size_t line_offset(int line) const {
        sys::size_t offset = 0;
        while (line > 0 && offset < length_) {
            if (text_[offset++] == '\n')
                --line;
        }
        return line > 0 ? length_ : offset;
    }

    // False when the text is full or the byte is not text.
    bool insert(char byte) {
        if (length_ == CAPACITY || !is_text(byte))
            return false;
        memmove(text_.data() + cursor_ + 1, text_.data() + cursor_, length_ - cursor_);
        text_[cursor_++] = byte;
        ++length_;
        edited();
        return true;
    }

    void erase_before() {
        if (cursor_ == 0)
            return;
        --cursor_;
        erase_after();
    }

    void erase_after() {
        if (cursor_ == length_)
            return;
        memmove(text_.data() + cursor_, text_.data() + cursor_ + 1, length_ - cursor_ - 1);
        --length_;
        edited();
    }

    void left() { move_to(cursor_ > 0 ? cursor_ - 1 : 0); }
    void right() { move_to(cursor_ < length_ ? cursor_ + 1 : length_); }
    void home() { move_to(line_start(cursor_)); }
    void end() { move_to(line_end(cursor_)); }
    void start() { move_to(0); }
    void finish() { move_to(length_); }

    // Moves up or down, keeping the column the cursor had before the first
    // vertical move.
    void vertical(int lines) {
        if (wanted_column_ < 0)
            wanted_column_ = column();
        int target = line() + lines;
        int last = line_count() - 1;
        cursor_ = at(target < 0 ? 0 : target > last ? last : target, wanted_column_);
    }

    // Puts the cursor on the closest position of a line to a display column.
    void place(int line, int column) {
        int last = line_count() - 1;
        move_to(at(line < 0 ? 0 : line > last ? last : line, column < 0 ? 0 : column));
    }

private:
    sys::vector<char> text_;
    sys::size_t length_{};
    sys::size_t cursor_{};
    // The column vertical moves aim for, or -1.
    int wanted_column_ = -1;
    sys::array<char, PROCESS_MAX_PATH> path_{};
    bool modified_{};

    sys::size_t line_start(sys::size_t position) const {
        while (position > 0 && text_[position - 1] != '\n')
            --position;
        return position;
    }

    sys::size_t line_end(sys::size_t position) const {
        while (position < length_ && text_[position] != '\n')
            ++position;
        return position;
    }

    // The last position on a line that starts at or before a display column.
    sys::size_t at(int line, int column) const {
        sys::size_t position = line_offset(line);
        int reached = 0;
        while (position < length_ && text_[position] != '\n') {
            int next = advance(reached, text_[position]);
            if (next > column)
                break;
            reached = next;
            ++position;
        }
        return position;
    }

    void move_to(sys::size_t position) {
        cursor_ = position;
        wanted_column_ = -1;
    }

    void edited() {
        modified_ = true;
        wanted_column_ = -1;
    }
};

}  // namespace editor
