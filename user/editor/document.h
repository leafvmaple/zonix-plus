#pragma once

#include "../command.h"
#include <sys/array.hpp>
#include <sys/vector.hpp>

namespace editor {

// The text being edited: its bytes, the cursor and selection, the edit
// history and the file it belongs to. It accepts the same files as vi (ASCII
// with LF line ends and tabs, at most 64 KiB) and knows nothing about how the
// text is shown.
class Document {
public:
    static constexpr sys::size_t CAPACITY = 65536;
    static constexpr int TAB_WIDTH = 8;
    // The oldest edits are forgotten beyond this many.
    static constexpr sys::size_t HISTORY_LIMIT = 1000;

    Document() { text_.resize(CAPACITY); }

    bool ready() const { return text_.size() == CAPACITY; }

    // Reads the file. A missing file starts an empty document under that name.
    long load(const char* path) {
        long error = set_path(path);
        if (error < 0)
            return error;
        FileStat st{};
        error = sys_stat(path_.data(), &st);
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
        if (untitled())
            return SYS_ERROR_INVALID;
        int fd = sys_open(path_.data(), O_WRONLY | O_CREAT | O_CLOEXEC);
        if (fd < 0)
            return fd;
        DescriptorStat st{};
        long error = sys_fstat(fd, &st);
        if (error == 0 && st.type != FILE_TYPE_FILE)
            error = SYS_ERROR_INVALID;
        if (error == 0) {
            saved_ = NEVER_SAVED;
            error = sys_ftruncate(fd, 0);
        }
        if (error == 0)
            error = command_write_all(fd, text_.data(), static_cast<long>(length_));
        if (sys_close(fd) < 0 && error == 0)
            error = SYS_ERROR_IO;
        if (error == 0)
            error = sys_sync();
        if (error == 0) {
            saved_ = static_cast<long>(applied_);
            // Typing after a save starts a new edit, so undo can reach the saved text.
            typing_ = false;
        }
        return error;
    }

    // Saves under another name, which the document keeps only if saving works.
    long save_as(const char* path) {
        sys::array<char, PROCESS_MAX_PATH> previous = path_;
        long error = set_path(path);
        if (error == 0)
            error = save();
        if (error < 0)
            path_ = previous;
        return error;
    }

    bool untitled() const { return path_[0] == 0; }
    const char* path() const { return path_.data(); }
    bool modified() const { return saved_ != static_cast<long>(applied_); }
    const char* data() const { return text_.data(); }
    sys::size_t size() const { return length_; }
    sys::size_t cursor() const { return cursor_; }

    bool has_selection() const { return anchor_ != NO_ANCHOR && anchor_ != cursor_; }
    sys::size_t selection_start() const { return has_selection() && anchor_ < cursor_ ? anchor_ : cursor_; }
    sys::size_t selection_end() const { return has_selection() && anchor_ > cursor_ ? anchor_ : cursor_; }

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

    // Typing replaces the selection. Consecutive characters typed in one
    // place undo together. False when the text is full or the byte is not text.
    bool type(char byte) {
        if (!is_text(byte))
            return false;
        bool join = typing_ && !has_selection() && applied_ > 0 && !can_redo() && byte != '\n' &&
                    history_[applied_ - 1].position + history_[applied_ - 1].inserted.size() == cursor_;
        if (!replace(selection_start(), selection_end(), &byte, 1, join))
            return false;
        typing_ = true;
        return true;
    }

    // Replaces the selection with text, such as a paste. Bytes that are not
    // text are rejected. False when the result would not fit.
    bool insert(const char* text, sys::size_t length) {
        for (sys::size_t i = 0; i < length; ++i) {
            if (!is_text(text[i]))
                return false;
        }
        typing_ = false;
        return replace(selection_start(), selection_end(), text, length, false);
    }

    // Deletes the selection, or else the character before or after the cursor.
    void erase_before() {
        typing_ = false;
        if (has_selection())
            replace(selection_start(), selection_end(), nullptr, 0, false);
        else if (cursor_ > 0)
            replace(cursor_ - 1, cursor_, nullptr, 0, false);
    }
    void erase_after() {
        typing_ = false;
        if (has_selection())
            replace(selection_start(), selection_end(), nullptr, 0, false);
        else if (cursor_ < length_)
            replace(cursor_, cursor_ + 1, nullptr, 0, false);
    }

    // The selected bytes are data()[selection_start(), selection_end()).
    void select_all() {
        anchor_ = 0;
        move_to(length_, true);
    }

    // Moves the cursor. With `extend` the selection grows from where it
    // started; without it the selection is dropped, and left or right moves to
    // its edge.
    void left(bool extend) {
        if (!extend && has_selection())
            move_to(selection_start(), false);
        else
            move_to(cursor_ > 0 ? cursor_ - 1 : 0, extend);
    }
    void right(bool extend) {
        if (!extend && has_selection())
            move_to(selection_end(), false);
        else
            move_to(cursor_ < length_ ? cursor_ + 1 : length_, extend);
    }
    void home(bool extend) { move_to(line_start(cursor_), extend); }
    void end(bool extend) { move_to(line_end(cursor_), extend); }
    void start(bool extend) { move_to(0, extend); }
    void finish(bool extend) { move_to(length_, extend); }

    // Moves up or down, keeping the column the cursor had before the first
    // vertical move.
    void vertical(int lines, bool extend) {
        int wanted = wanted_column_ < 0 ? column() : wanted_column_;
        int target = line() + lines;
        int last = line_count() - 1;
        move_to(at(target < 0 ? 0 : target > last ? last : target, wanted), extend);
        wanted_column_ = wanted;
    }

    // Puts the cursor on the closest position of a line to a display column.
    void place(int line, int column, bool extend) {
        int last = line_count() - 1;
        move_to(at(line < 0 ? 0 : line > last ? last : line, column < 0 ? 0 : column), extend);
    }

    bool can_undo() const { return applied_ > 0; }
    bool can_redo() const { return applied_ < history_.size(); }

    // Takes back the last edit, restoring the text and the selection it had.
    void undo() {
        if (!can_undo())
            return;
        const Edit& edit = history_[--applied_];
        splice(edit.position, edit.position + edit.inserted.size(), edit.removed.data(), edit.removed.size());
        cursor_ = edit.cursor_before;
        anchor_ = edit.anchor_before;
        typing_ = false;
        settle();
    }

    // Makes the last undone edit again.
    void redo() {
        if (!can_redo())
            return;
        const Edit& edit = history_[applied_++];
        splice(edit.position, edit.position + edit.removed.size(), edit.inserted.data(), edit.inserted.size());
        cursor_ = edit.position + edit.inserted.size();
        anchor_ = NO_ANCHOR;
        typing_ = false;
        settle();
    }

    // Selects the next occurrence after the selection or cursor, wrapping
    // around to the start. Returns false and leaves the selection when there
    // is none.
    bool find(const char* needle, sys::size_t length) {
        if (length == 0 || length > length_)
            return false;
        sys::size_t from = selection_end();
        for (sys::size_t step = 0; step <= length_; ++step) {
            sys::size_t position = (from + step) % (length_ + 1);
            if (position + length <= length_ && memcmp(text_.data() + position, needle, length) == 0) {
                anchor_ = position;
                move_to(position + length, true);
                return true;
            }
        }
        return false;
    }

private:
    static constexpr sys::size_t NO_ANCHOR = ~sys::size_t{0};
    // The value of saved_ when no state in the history matches the file.
    static constexpr long NEVER_SAVED = -1;

    // One change: `removed` was replaced by `inserted` at `position`.
    struct Edit {
        sys::size_t position{};
        sys::vector<char> removed;
        sys::vector<char> inserted;
        sys::size_t cursor_before{};
        sys::size_t anchor_before{};
    };

    sys::vector<char> text_;
    sys::size_t length_{};
    sys::size_t cursor_{};
    // Where the selection started, or NO_ANCHOR.
    sys::size_t anchor_ = NO_ANCHOR;
    // The column vertical moves aim for, or -1.
    int wanted_column_ = -1;
    sys::array<char, PROCESS_MAX_PATH> path_{};
    sys::vector<Edit> history_;
    // How many edits of the history the text includes. Later ones were undone.
    sys::size_t applied_{};
    // The value of applied_ when the text matched the file, or NEVER_SAVED.
    long saved_{};
    // The last edit was typing, which the next typed character may extend.
    bool typing_{};

    // An empty path is an untitled document.
    long set_path(const char* path) {
        if (!path || path[0] == 0) {
            path_[0] = 0;
            return 0;
        }
        sys::size_t name = strlen(const_cast<char*>(path));
        if (path[0] != '/' || name >= path_.size())
            return SYS_ERROR_INVALID;
        memcpy(path_.data(), path, name + 1);
        return 0;
    }

    // Replaces [start, end) with text and records it, extending the last edit
    // when `join` is set. False and unchanged when the result would not fit.
    bool replace(sys::size_t start, sys::size_t end, const char* text, sys::size_t length, bool join) {
        if (length_ - (end - start) + length > CAPACITY)
            return false;
        if (start == end && length == 0)
            return true;
        if (join) {
            auto& last = history_[applied_ - 1];
            for (sys::size_t i = 0; i < length; ++i)
                last.inserted.push_back(text[i]);
        } else {
            // A new edit discards the undone ones.
            history_.erase(history_.begin() + static_cast<long>(applied_), history_.end());
            if (saved_ > static_cast<long>(applied_))
                saved_ = NEVER_SAVED;
            Edit edit;
            edit.position = start;
            edit.cursor_before = cursor_;
            edit.anchor_before = anchor_;
            for (sys::size_t i = start; i < end; ++i)
                edit.removed.push_back(text_[i]);
            for (sys::size_t i = 0; i < length; ++i)
                edit.inserted.push_back(text[i]);
            history_.push_back(sys::move(edit));
            ++applied_;
            if (history_.size() > HISTORY_LIMIT) {
                history_.erase(history_.begin());
                --applied_;
                saved_ = saved_ > 0 ? saved_ - 1 : NEVER_SAVED;
            }
        }
        splice(start, end, text, length);
        cursor_ = start + length;
        anchor_ = NO_ANCHOR;
        settle();
        return true;
    }

    // Puts text in place of [start, end) without recording anything.
    void splice(sys::size_t start, sys::size_t end, const char* text, sys::size_t length) {
        memmove(text_.data() + start + length, text_.data() + end, length_ - end);
        if (length)
            memcpy(text_.data() + start, text, length);
        length_ = length_ - (end - start) + length;
    }

    void settle() {
        if (cursor_ > length_)
            cursor_ = length_;
        if (anchor_ != NO_ANCHOR && anchor_ > length_)
            anchor_ = NO_ANCHOR;
        wanted_column_ = -1;
    }

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

    void move_to(sys::size_t position, bool extend) {
        if (extend && anchor_ == NO_ANCHOR)
            anchor_ = cursor_;
        else if (!extend)
            anchor_ = NO_ANCHOR;
        cursor_ = position;
        wanted_column_ = -1;
        typing_ = false;
    }
};

}  // namespace editor
