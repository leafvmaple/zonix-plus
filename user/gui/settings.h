#pragma once

#include "../command.h"

namespace zonix_gui {

// Preferences shared by Desktop and its programs. They are kept in
// /etc/settings as `key=value` lines; a missing file, key or unreadable value
// gives the default. This header has no engine dependency.
struct Settings {
    static constexpr const char* PATH = "/etc/settings";
    static constexpr int TIMEZONE_STEP = 30;
    static constexpr int MIN_TIMEZONE = -12 * 60;
    static constexpr int MAX_TIMEZONE = 14 * 60;
    static constexpr int TEXT_SIZES[] = {14, 16, 18};
    static constexpr int DEFAULT_TEXT_SIZE = 16;
    // "+HH:MM" and its terminator.
    static constexpr int OFFSET_CAPACITY = 7;

    // The local time's offset from UTC, in minutes.
    int timezone_minutes = 0;
    // The pixel size of interface text.
    int text_size = DEFAULT_TEXT_SIZE;

    static Settings load() {
        Settings settings;
        char text[128]{};
        int fd = sys_open(const_cast<char*>(PATH), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return settings;
        long count = sys_read(fd, text, sizeof(text) - 1);
        sys_close(fd);
        for (long start = 0; start < count;) {
            long end = start;
            while (end < count && text[end] != '\n')
                ++end;
            text[end] = 0;
            settings.apply(text + start);
            start = end + 1;
        }
        return settings;
    }

    // Replaces the file and syncs it.
    long save() const {
        char text[64];
        char* out = put(text, "timezone=");
        format_offset(out, timezone_minutes);
        out += OFFSET_CAPACITY - 1;
        out = put(out, "\ntext_size=");
        *out++ = static_cast<char>('0' + text_size / 10);
        *out++ = static_cast<char>('0' + text_size % 10);
        *out++ = '\n';
        int fd = sys_open(const_cast<char*>(PATH), O_WRONLY | O_CREAT | O_CLOEXEC);
        if (fd < 0)
            return fd;
        long error = sys_ftruncate(fd, 0);
        if (error == 0)
            error = command_write_all(fd, text, out - text);
        if (sys_close(fd) < 0 && error == 0)
            error = SYS_ERROR_IO;
        return error == 0 ? sys_sync() : error;
    }

    // Writes an offset as "+HH:MM" or "-HH:MM".
    static void format_offset(char* out, int minutes) {
        int magnitude = minutes < 0 ? -minutes : minutes;
        out[0] = minutes < 0 ? '-' : '+';
        out[1] = static_cast<char>('0' + magnitude / 60 / 10);
        out[2] = static_cast<char>('0' + magnitude / 60 % 10);
        out[3] = ':';
        out[4] = static_cast<char>('0' + magnitude % 60 / 10);
        out[5] = static_cast<char>('0' + magnitude % 60 % 10);
        out[6] = 0;
    }

private:
    static char* put(char* out, const char* text) {
        while (*text != 0)
            *out++ = *text++;
        return out;
    }

    // The value of a line that starts with `key=`, or null.
    static const char* value_of(const char* line, const char* key) {
        while (*key != 0) {
            if (*line++ != *key++)
                return nullptr;
        }
        return *line == '=' ? line + 1 : nullptr;
    }

    static int digit(char c) { return c >= '0' && c <= '9' ? c - '0' : -1; }

    void apply(const char* line) {
        if (const char* value = value_of(line, "timezone")) {
            if ((value[0] != '+' && value[0] != '-') || digit(value[1]) < 0 || digit(value[2]) < 0 ||
                value[3] != ':' || digit(value[4]) < 0 || digit(value[5]) < 0)
                return;
            int minutes = (digit(value[1]) * 10 + digit(value[2])) * 60 + digit(value[4]) * 10 + digit(value[5]);
            if (value[0] == '-')
                minutes = -minutes;
            if (minutes >= MIN_TIMEZONE && minutes <= MAX_TIMEZONE)
                timezone_minutes = minutes;
        } else if (const char* size = value_of(line, "text_size")) {
            if (digit(size[0]) < 0 || digit(size[1]) < 0)
                return;
            int parsed = digit(size[0]) * 10 + digit(size[1]);
            for (int allowed : TEXT_SIZES) {
                if (parsed == allowed)
                    text_size = parsed;
            }
        }
    }
};

}  // namespace zonix_gui
