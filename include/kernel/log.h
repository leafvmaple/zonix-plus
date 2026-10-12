#pragma once

#include <base/types.h>

namespace klog {

enum class Level : uint8_t { Debug, Info, Warn, Error };

// Messages include their subsystem and end with a newline. Recording is
// independent of the console threshold. Ordinary console/test output uses cprintf.
void write(Level level, const char* fmt, ...);
void dump();
void set_console_level(Level level);
Level console_level();
const char* level_name(Level level);

template<typename... Args>
void debug(const char* fmt, Args... args) {
    write(Level::Debug, fmt, args...);
}

template<typename... Args>
void info(const char* fmt, Args... args) {
    write(Level::Info, fmt, args...);
}

template<typename... Args>
void warn(const char* fmt, Args... args) {
    write(Level::Warn, fmt, args...);
}

template<typename... Args>
void error(const char* fmt, Args... args) {
    write(Level::Error, fmt, args...);
}

}  // namespace klog
