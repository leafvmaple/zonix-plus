#pragma once

#define SHELL_LINE_LIMIT    255
#define SHELL_HISTORY_LIMIT 16

struct LineEditor {
    char history[SHELL_HISTORY_LIMIT][SHELL_LINE_LIMIT + 1];
    int count;
    int next;
};

/* Enter returns a newline-terminated line; EOF and Ctrl-D can return a partial
 * line. Cancelled/overlong lines never enter history. Non-console input keeps
 * the existing CLI line policy. The caller provides SHELL_LINE_LIMIT+2 bytes. */
long line_editor_read(struct LineEditor* editor, char* line, const char* cwd, int* skip_lf);
