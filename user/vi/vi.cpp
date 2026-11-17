#include "../command.h"
#include <sys/array.hpp>

class Editor {
public:
    static int run(int argc, char** argv) {
        if (argc > 2)
            return command_usage("Usage: vi [file] (ASCII/LF text, at most 64 KiB)\n");
        TerminalSize output{};
        long input_result = sys_ioctl(STDIN_FD, TERMINAL_GET_SIZE, reinterpret_cast<long>(&size_));
        long output_result = sys_ioctl(STDOUT_FD, TERMINAL_GET_SIZE, reinterpret_cast<long>(&output));
        if (input_result < 0 || output_result < 0)
            return command_error("vi", "terminal", input_result < 0 ? input_result : output_result);
        if (size_.rows < 4 || size_.cols < 16 || size_.rows != output.rows || size_.cols != output.cols)
            return command_error("vi", "terminal", SYS_ERROR_NOT_SUPPORTED);
        if (argc == 2 && !load(argv[1]))
            return 1;
        long mode_result = sys_ioctl(STDIN_FD, TERMINAL_SET_RAW, 1);
        if (mode_result < 0)
            return command_error("vi", "terminal mode", mode_result);
        bool failed = !text("\x1b[?25l\x1b[2J\x1b[H");
        message("i: insert  Esc: normal  :w :q :wq :q!");
        while (!quit_ && !failed) {
            failed = !render();
            if (failed)
                break;
            unsigned char key{};
            if (sys_read(STDIN_FD, &key, 1) != 1) {
                failed = true;
                break;
            }
            handle(key);
        }
        // Kernel ownership also releases raw input and restores the display on
        // faults/termination, so cleanup does not depend on reaching this call.
        if (sys_ioctl(STDIN_FD, TERMINAL_SET_RAW, 0) < 0)
            failed = true;
        return failed ? command_error("vi", "terminal I/O", SYS_ERROR_IO) : 0;
    }

private:
    static constexpr unsigned long CAPACITY = 65536;
    enum class Mode { Normal, Insert, Command };
    enum class Escape { None, Prefix, Csi };
    inline static sys::array<char, CAPACITY + 1> data_{};
    inline static unsigned long length_{};
    inline static unsigned long cursor_{};
    inline static unsigned int top_{};
    inline static unsigned int left_{};
    inline static unsigned int preferred_col_{};
    inline static TerminalSize size_{};
    inline static sys::array<char, PROCESS_MAX_PATH> filename_{};
    inline static sys::array<char, PROCESS_MAX_PATH + 8> command_{};
    inline static unsigned int command_length_{};
    inline static sys::array<char, 160> message_{};
    inline static Mode mode_{};
    inline static Mode escape_mode_{};
    inline static Escape escape_{};
    inline static unsigned int escape_number_{};
    inline static unsigned long escape_cursor_{};
    inline static unsigned int escape_column_{};
    inline static bool dirty_{};
    inline static bool quit_{};
    inline static bool pending_delete_{};
    inline static bool redraw_ = true;
    inline static unsigned int redraw_from_{};
    inline static unsigned int redraw_to_ = 0xFFFFFFFF;

    static bool text(const char* value) { return command_text(STDOUT_FD, value) == 0; }
    static bool bytes(const char* value, unsigned long count) {
        return command_write_all(STDOUT_FD, value, static_cast<long>(count)) == 0;
    }
    static void message(const char* value) {
        unsigned int i = 0;
        while (value[i] && i + 1 < message_.size()) {
            message_[i] = value[i];
            ++i;
        }
        message_[i] = 0;
    }
    static bool load(char* path) {
        long path_length = strlen(path);
        if (path_length == 0 || path_length >= PROCESS_MAX_PATH) {
            command_error("vi", path, SYS_ERROR_INVALID);
            return false;
        }
        struct FileStat st {};
        long error = sys_stat(path, &st);
        if (error == SYS_ERROR_NOT_FOUND) {
            memcpy(filename_.data(), path, path_length + 1);
            return true;
        }
        if (error < 0 || st.type != FILE_TYPE_FILE || st.size > CAPACITY) {
            command_error("vi", path, error < 0 ? error : SYS_ERROR_NOT_SUPPORTED);
            return false;
        }
        int fd = sys_open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            command_error("vi", path, fd);
            return false;
        }
        while (length_ <= CAPACITY) {
            long count = sys_read(fd, data_.data() + length_, static_cast<long>(data_.size() - length_));
            if (count < 0) {
                error = count;
                break;
            }
            if (count == 0)
                break;
            length_ += static_cast<unsigned long>(count);
        }
        if (sys_close(fd) < 0)
            error = SYS_ERROR_IO;
        if (length_ > CAPACITY)
            error = SYS_ERROR_FULL;
        for (unsigned long i = 0; error == 0 && i < length_; ++i) {
            unsigned char c = static_cast<unsigned char>(data_[i]);
            if (c != '\n' && c != '\t' && (c < 32 || c > 126))
                error = SYS_ERROR_NOT_SUPPORTED;
        }
        if (error < 0) {
            command_error("vi", path, error);
            return false;
        }
        memcpy(filename_.data(), path, path_length + 1);
        return true;
    }
    static unsigned long line_start(unsigned long position) {
        while (position > 0 && data_[position - 1] != '\n')
            --position;
        return position;
    }
    static unsigned long line_end(unsigned long position) {
        while (position < length_ && data_[position] != '\n')
            ++position;
        return position;
    }
    static unsigned int line_number(unsigned long position) {
        unsigned int line = 0;
        for (unsigned long i = 0; i < position; ++i)
            if (data_[i] == '\n')
                ++line;
        return line;
    }
    static unsigned int column(unsigned long position) {
        unsigned int col = 0;
        for (unsigned long i = line_start(position); i < position; ++i)
            col += data_[i] == '\t' ? 8 - col % 8 : 1;
        return col;
    }
    static void clamp_normal() {
        if (mode_ == Mode::Normal && cursor_ == line_end(cursor_) && cursor_ > line_start(cursor_))
            --cursor_;
    }
    static void move_vertical(int direction) {
        unsigned long start = line_start(cursor_);
        if (direction < 0) {
            if (start == 0)
                return;
            start = line_start(start - 1);
        } else {
            unsigned long end = line_end(cursor_);
            if (end == length_)
                return;
            start = end + 1;
        }
        unsigned int col = 0;
        cursor_ = start;
        while (cursor_ < length_ && data_[cursor_] != '\n') {
            unsigned int next = col + (data_[cursor_] == '\t' ? 8 - col % 8 : 1);
            if (next > preferred_col_)
                break;
            col = next;
            ++cursor_;
        }
        clamp_normal();
    }
    static void move_horizontal(int direction) {
        if (direction < 0) {
            if (cursor_ > line_start(cursor_))
                --cursor_;
        } else if (cursor_ < line_end(cursor_)) {
            ++cursor_;
            clamp_normal();
        }
        preferred_col_ = column(cursor_);
    }
    static void changed(unsigned int line, bool structure) {
        dirty_ = true;
        if (!redraw_ || line < redraw_from_)
            redraw_from_ = line;
        unsigned int end = structure ? 0xFFFFFFFF : line;
        if (!redraw_ || end > redraw_to_)
            redraw_to_ = end;
        redraw_ = true;
        message("");
        preferred_col_ = column(cursor_);
    }
    static void insert(char c) {
        if (length_ == CAPACITY) {
            message("Buffer full (64 KiB)");
            return;
        }
        unsigned int line = line_number(cursor_);
        memmove(data_.data() + cursor_ + 1, data_.data() + cursor_, length_ - cursor_);
        data_[cursor_++] = c;
        ++length_;
        changed(line, c == '\n');
    }
    static void erase(unsigned long start, unsigned long end) {
        if (start == end)
            return;
        unsigned int line = line_number(start);
        bool structure = false;
        for (unsigned long i = start; i < end; ++i)
            if (data_[i] == '\n')
                structure = true;
        memmove(data_.data() + start, data_.data() + end, length_ - end);
        length_ -= end - start;
        cursor_ = start;
        clamp_normal();
        changed(line, structure);
    }
    static bool save(const char* supplied) {
        const char* path = *supplied ? supplied : filename_.data();
        long count = strlen((char*)path);
        if (count == 0) {
            message("No file name; use :w path");
            return false;
        }
        if (count >= PROCESS_MAX_PATH) {
            message("File name too long");
            return false;
        }
        int fd = sys_open((char*)path, O_WRONLY | O_CREAT | O_CLOEXEC);
        if (fd < 0) {
            message("Cannot open for writing; buffer kept");
            return false;
        }
        DescriptorStat st{};
        long error = sys_fstat(fd, &st);
        if (error == 0 && st.type != FILE_TYPE_FILE)
            error = SYS_ERROR_INVALID;
        if (error == 0) {
            dirty_ = true;  // Retain the complete buffer even if write-back fails.
            error = sys_ftruncate(fd, 0);
        }
        if (error == 0)
            error = command_write_all(fd, data_.data(), static_cast<long>(length_));
        if (sys_close(fd) < 0)
            error = SYS_ERROR_IO;
        if (error == 0)
            error = sys_sync();
        if (error < 0) {
            message("Write/sync failed; buffer kept (file may be partial)");
            return false;
        }
        memcpy(filename_.data(), (void*)path, count + 1);
        dirty_ = false;
        message("Written");
        return true;
    }
    static void execute() {
        command_[command_length_] = 0;
        char* word = command_.data();
        char* path = word;
        while (*path && *path != ' ' && *path != '\t')
            ++path;
        if (*path)
            *path++ = 0;
        while (*path == ' ' || *path == '\t')
            ++path;
        if (!strcmp(word, (char*)"w") || !strcmp(word, (char*)"wq")) {
            bool written = save(path);
            if (written && word[1] == 'q')
                quit_ = true;
        } else if (!strcmp(word, (char*)"q") && !*path) {
            if (dirty_)
                message("Unsaved changes; :w to save or :q! to discard");
            else
                quit_ = true;
        } else if (!strcmp(word, (char*)"q!") && !*path) {
            quit_ = true;
        } else {
            message("Commands: :w [path]  :q  :wq [path]  :q!");
        }
        mode_ = Mode::Normal;
        command_length_ = 0;
        clamp_normal();
    }
    static void arrow(int key) {
        if (mode_ == Mode::Command)
            return;
        if (key == 'A')
            move_vertical(-1);
        else if (key == 'B')
            move_vertical(1);
        else if (key == 'C')
            move_horizontal(1);
        else if (key == 'D')
            move_horizontal(-1);
        else if (key == 'H') {
            cursor_ = line_start(cursor_);
            preferred_col_ = 0;
        } else if (key == 'F') {
            cursor_ = line_end(cursor_);
            clamp_normal();
            preferred_col_ = column(cursor_);
        } else if (key == '~' && escape_number_ == 3 && cursor_ < length_)
            erase(cursor_, cursor_ + 1);
    }
    static void handle(unsigned char key) {
        if (key == 27 || key == 3) {
            escape_mode_ = mode_;
            escape_cursor_ = cursor_;
            escape_column_ = preferred_col_;
            escape_ = key == 27 ? Escape::Prefix : Escape::None;
            mode_ = Mode::Normal;
            pending_delete_ = false;
            clamp_normal();
            preferred_col_ = column(cursor_);
            message(key == 3 ? "Interrupted; buffer kept" : "");
            return;
        }
        if (escape_ == Escape::Prefix) {
            escape_ = Escape::None;
            if (key == '[' || key == 'O') {
                escape_ = Escape::Csi;
                escape_number_ = 0;
                return;
            }
        } else if (escape_ == Escape::Csi) {
            if (key >= '0' && key <= '9') {
                if (escape_number_ < 100)
                    escape_number_ = escape_number_ * 10 + key - '0';
                return;
            }
            if (key < 0x40 || key > 0x7E)
                return;
            escape_ = Escape::None;
            mode_ = escape_mode_;
            cursor_ = escape_cursor_;
            preferred_col_ = escape_column_;
            arrow(key);
            return;
        }
        if (mode_ == Mode::Command) {
            if (key == '\n' || key == '\r')
                execute();
            else if (key == 8 || key == 127) {
                if (command_length_)
                    --command_length_;
            } else if (key >= 32 && key <= 126) {
                if (command_length_ + 1 < command_.size())
                    command_[command_length_++] = static_cast<char>(key);
                else
                    message("Command too long");
            }
            return;
        }
        if (mode_ == Mode::Insert) {
            if (key == 8 || key == 127) {
                if (cursor_)
                    erase(cursor_ - 1, cursor_);
            } else if (key == '\r' || key == '\n')
                insert('\n');
            else if (key == '\t' || (key >= 32 && key <= 126))
                insert(static_cast<char>(key));
            return;
        }
        if (pending_delete_) {
            pending_delete_ = false;
            if (key == 'd') {
                unsigned long start = line_start(cursor_);
                unsigned long end = line_end(cursor_);
                if (end < length_)
                    ++end;
                else if (start > 0)
                    --start;
                erase(start, end);
                return;
            }
        }
        switch (key) {
            case 'i': mode_ = Mode::Insert; break;
            case 'a':
                if (cursor_ < line_end(cursor_))
                    ++cursor_;
                mode_ = Mode::Insert;
                preferred_col_ = column(cursor_);
                break;
            case 'h': move_horizontal(-1); break;
            case 'j': move_vertical(1); break;
            case 'k': move_vertical(-1); break;
            case 'l': move_horizontal(1); break;
            case '0':
                cursor_ = line_start(cursor_);
                preferred_col_ = 0;
                break;
            case '$':
                cursor_ = line_end(cursor_);
                clamp_normal();
                preferred_col_ = column(cursor_);
                break;
            case 'x':
                if (cursor_ < length_ && data_[cursor_] != '\n')
                    erase(cursor_, cursor_ + 1);
                break;
            case 'd': pending_delete_ = true; break;
            case ':':
                mode_ = Mode::Command;
                command_length_ = 0;
                break;
            default: break;
        }
    }
    static bool move_to(unsigned int row, unsigned int col) {
        return printf("\x1b[%d;%dH", static_cast<int>(row + 1), static_cast<int>(col + 1)) >= 0;
    }
    static bool clipped(const char* value, unsigned int limit) {
        unsigned long count = static_cast<unsigned long>(strlen((char*)value));
        return bytes(value, count < limit ? count : limit);
    }
    static bool render() {
        unsigned int body_rows = size_.rows - 2;
        unsigned int width = size_.cols - 1;  // Never trigger bottom-right autowrap.
        unsigned int line = line_number(cursor_);
        unsigned int col = column(cursor_);
        unsigned int top = top_, left = left_;
        if (line < top_)
            top_ = line;
        if (line >= top_ + body_rows)
            top_ = line - body_rows + 1;
        if (col < left_)
            left_ = col < width ? 0 : col;
        if (col >= left_ + width)
            left_ = col - width + 1;
        if (top != top_ || left != left_) {
            redraw_ = true;
            redraw_from_ = top_;
            redraw_to_ = 0xFFFFFFFF;
        }
        if (!text("\x1b[?25l"))
            return false;
        if (redraw_) {
            unsigned long position = 0;
            for (unsigned int skipped = 0; skipped < top_ && position < length_; ++skipped)
                position = line_end(position) + 1;
            for (unsigned int row = 0; row < body_rows; ++row) {
                unsigned long end = line_end(position);
                if (top_ + row >= redraw_from_ && top_ + row <= redraw_to_) {
                    if (!move_to(row, 0))
                        return false;
                    char output[256];
                    unsigned int used = 0, visual = 0;
                    if (position > length_) {
                        if (!text("~"))
                            return false;
                    } else
                        for (unsigned long i = position; i < end; ++i) {
                            unsigned int cells = data_[i] == '\t' ? 8 - visual % 8 : 1;
                            for (unsigned int cell = 0; cell < cells; ++cell, ++visual) {
                                if (visual >= left_ && visual - left_ < width)
                                    output[used++] = data_[i] == '\t' ? ' ' : data_[i];
                                if (used == sizeof(output)) {
                                    if (!bytes(output, used))
                                        return false;
                                    used = 0;
                                }
                            }
                        }
                    if (!bytes(output, used) || !text("\x1b[K"))
                        return false;
                }
                position = end < length_ ? end + 1 : length_ + 1;
            }
            redraw_ = false;
        }
        if (!move_to(size_.rows - 2, 0))
            return false;
        const char* mode = mode_ == Mode::Insert ? "INSERT " : (mode_ == Mode::Command ? "COMMAND " : "NORMAL ");
        if (!text(mode) || !text(dirty_ ? "[+] " : "    "))
            return false;
        if (!clipped(filename_[0] ? filename_.data() : "[No Name]", width - 12) || !text("\x1b[K"))
            return false;
        if (!move_to(size_.rows - 1, 0))
            return false;
        if (mode_ == Mode::Command) {
            command_[command_length_] = 0;
            if (!text(":") || !clipped(command_.data(), width - 1))
                return false;
        } else if (!clipped(message_.data(), width))
            return false;
        if (!text("\x1b[K"))
            return false;
        unsigned int command_col = command_length_ + 1 < width ? command_length_ + 1 : width - 1;
        return move_to(mode_ == Mode::Command ? size_.rows - 1 : line - top_,
                       mode_ == Mode::Command ? command_col : col - left_) &&
               text("\x1b[?25h");
    }
};

extern "C" int main(int argc, char** argv) {
    return Editor::run(argc, argv);
}
