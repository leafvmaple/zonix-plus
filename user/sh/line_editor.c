#include "line_editor.h"
#include "../command.h"

struct EditLine {
    char* text;
    int length;
    int cursor;
    unsigned int left;
    int overflow;
    int selection;
    char draft[SHELL_LINE_LIMIT + 1];
    int draft_cursor;
    unsigned int draft_left;
    int draft_overflow;
    char prompt[PROCESS_MAX_PATH + 4];
    unsigned int prompt_length;
    unsigned int width;
};

static unsigned int column(struct EditLine* edit, int end) {
    unsigned int col = 0;
    for (int i = 0; i < end; ++i)
        col += edit->text[i] == '\t' ? 8 - (edit->prompt_length + col) % 8 : 1;
    return col;
}

static long render(struct EditLine* edit) {
    unsigned int cursor = column(edit, edit->cursor);
    if (cursor < edit->left)
        edit->left = cursor;
    if (cursor >= edit->left + edit->width)
        edit->left = cursor - edit->width + 1;
    if (command_text(STDERR_FD, "\r") < 0 || command_text(STDERR_FD, edit->prompt) < 0)
        return SYS_ERROR_IO;
    char output[128];
    unsigned int used = 0, visual = 0;
    for (int i = 0; i < edit->length && visual < edit->left + edit->width; ++i) {
        unsigned int cells = edit->text[i] == '\t' ? 8 - (edit->prompt_length + visual) % 8 : 1;
        for (unsigned int cell = 0; cell < cells; ++cell, ++visual) {
            if (visual >= edit->left && visual - edit->left < edit->width)
                output[used++] = edit->text[i] == '\t' ? ' ' : edit->text[i];
            if (used == sizeof(output)) {
                if (command_write_all(STDERR_FD, output, used) < 0)
                    return SYS_ERROR_IO;
                used = 0;
            }
        }
    }
    if (command_write_all(STDERR_FD, output, used) < 0 || command_text(STDERR_FD, "\x1b[K") < 0)
        return SYS_ERROR_IO;
    // Position on this row without erasing text, including framebuffer glyphs.
    char move[16] = {27, '['};
    char digits[10];
    int count = 0, length = 2;
    unsigned int col = edit->prompt_length + cursor - edit->left + 1;
    do {
        digits[count++] = (char)('0' + col % 10);
        col /= 10;
    } while (col);
    while (count)
        move[length++] = digits[--count];
    move[length++] = 'G';
    return command_write_all(STDERR_FD, move, length);
}

static void remember(struct LineEditor* editor, char* line) {
    const char* nonblank = line;
    while (*nonblank == ' ' || *nonblank == '\t')
        ++nonblank;
    if (!*nonblank)
        return;
    int latest = (editor->next + SHELL_HISTORY_LIMIT - 1) % SHELL_HISTORY_LIMIT;
    if (editor->count && !strcmp(editor->history[latest], line))
        return;
    memcpy(editor->history[editor->next], line, strlen(line) + 1);
    editor->next = (editor->next + 1) % SHELL_HISTORY_LIMIT;
    if (editor->count < SHELL_HISTORY_LIMIT)
        ++editor->count;
}

static void history_move(struct LineEditor* editor, struct EditLine* edit, int direction) {
    int selection = edit->selection + direction;
    if (selection < 0 || selection > editor->count)
        return;
    if (edit->selection == editor->count) {
        memcpy(edit->draft, edit->text, edit->length + 1);
        edit->draft_cursor = edit->cursor;
        edit->draft_left = edit->left;
        edit->draft_overflow = edit->overflow;
    }
    edit->selection = selection;
    if (selection == editor->count) {
        memcpy(edit->text, edit->draft, strlen(edit->draft) + 1);
        edit->cursor = edit->draft_cursor;
        edit->left = edit->draft_left;
        edit->overflow = edit->draft_overflow;
    } else {
        int slot = (editor->next + SHELL_HISTORY_LIMIT - editor->count + selection) % SHELL_HISTORY_LIMIT;
        memcpy(edit->text, editor->history[slot], strlen(editor->history[slot]) + 1);
        edit->cursor = (int)strlen(edit->text);
        edit->left = 0;
        edit->overflow = 0;
    }
    edit->length = (int)strlen(edit->text);
}

static void erase(struct EditLine* edit, int position) {
    memmove(edit->text + position, edit->text + position + 1, edit->length - position);
    --edit->length;
}

long line_editor_read(struct LineEditor* editor, char* line, const char* cwd, int* skip_lf) {
    struct TerminalSize input, output, error;
    if (sys_ioctl(STDIN_FD, TERMINAL_GET_SIZE, (long)&input) < 0 ||
        sys_ioctl(STDOUT_FD, TERMINAL_GET_SIZE, (long)&output) < 0 ||
        sys_ioctl(STDERR_FD, TERMINAL_GET_SIZE, (long)&error) < 0 || input.cols < 16 || input.cols != output.cols ||
        input.cols != error.cols || input.rows != output.rows || input.rows != error.rows) {
        if (printf("%s $ ", cwd) < 0)
            return SYS_ERROR_IO;
        return command_read_line(line, SHELL_LINE_LIMIT + 2, skip_lf);
    }
    struct EditLine edit = {0};
    edit.text = line;
    edit.selection = editor->count;
    line[0] = 0;
    unsigned int path_limit = (input.cols - 1) / 2 - 3;
    unsigned int path_length = (unsigned int)strlen((char*)cwd);
    if (path_length > path_limit) {
        memcpy(edit.prompt, "...", 3);
        memcpy(edit.prompt + 3, (void*)(cwd + path_length - (path_limit - 3)), path_limit - 3);
        edit.prompt_length = path_limit;
    } else {
        memcpy(edit.prompt, (void*)cwd, path_length);
        edit.prompt_length = path_length;
    }
    memcpy(edit.prompt + edit.prompt_length, " $ ", 4);
    edit.prompt_length += 3;
    edit.width = input.cols - 1 - edit.prompt_length;
    // A fresh prompt row preserves program output that did not end with LF.
    if (command_text(STDERR_FD, "\n") < 0)
        return SYS_ERROR_IO;
    int escape = 0, number = 0, parameter = 0;
    for (;;) {
        if (render(&edit) < 0)
            return SYS_ERROR_IO;
        unsigned char key;
        long count = sys_read(STDIN_FD, &key, 1);
        if (count < 0)
            return SYS_ERROR_IO;
        if (count == 0 || key == 4 || key == '\r' || key == '\n') {
            if (count && *skip_lf && key == '\n') {
                *skip_lf = 0;
                continue;
            }
            *skip_lf = count && key == '\r';
            if (command_text(STDERR_FD, "\n") < 0)
                return SYS_ERROR_IO;
            if (edit.overflow)
                return SYS_ERROR_FULL;
            remember(editor, line);
            if (count && (key == '\r' || key == '\n'))
                line[edit.length++] = '\n';
            line[edit.length] = 0;
            return edit.length;
        }
        *skip_lf = 0;
        if (key == 3) {
            line[0] = 0;
            return command_text(STDERR_FD, "\r\x1b[2K^C\n") < 0 ? SYS_ERROR_IO : SYS_ERROR_INTERRUPTED;
        }
        if (key == 27) {
            escape = 1;
            continue;
        }
        if (escape == 1) {
            escape = 0;
            if (key == '[' || key == 'O') {
                escape = 2;
                number = parameter = 0;
                continue;
            }
        } else if (escape == 2) {
            if (key >= '0' && key <= '9') {
                if (parameter == 0 && number < 10000)
                    number = number * 10 + key - '0';
                continue;
            }
            if (key < 0x40 || key > 0x7E) {
                if (key == ';')
                    parameter = 1;
                continue;
            }
            escape = 0;
            if (key == 'A')
                history_move(editor, &edit, -1);
            else if (key == 'B')
                history_move(editor, &edit, 1);
            else if (key == 'D' && edit.cursor > 0)
                --edit.cursor;
            else if (key == 'C' && edit.cursor < edit.length)
                ++edit.cursor;
            else if (key == 'H' || (key == '~' && (number == 1 || number == 7)))
                edit.cursor = 0;
            else if (key == 'F' || (key == '~' && (number == 4 || number == 8)))
                edit.cursor = edit.length;
            else if (key == '~' && number == 3 && edit.cursor < edit.length)
                erase(&edit, edit.cursor);
            continue;
        }
        if (key == 8 || key == 127) {
            if (edit.cursor > 0)
                erase(&edit, --edit.cursor);
        } else if (key == '\t' || (key >= 32 && key <= 126)) {
            if (edit.length == SHELL_LINE_LIMIT)
                edit.overflow = 1;
            else if (!edit.overflow) {
                memmove(line + edit.cursor + 1, line + edit.cursor, edit.length - edit.cursor + 1);
                line[edit.cursor++] = (char)key;
                ++edit.length;
            }
        }
    }
}
