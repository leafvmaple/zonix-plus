#pragma once

#include <sys/cstring.hpp>

using sys::strchr;
using sys::strcmp;
using sys::strcpy;
using sys::strlen;
using sys::strncpy;

static inline bool str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) {
        return false;
    }

    while (*prefix) {
        if (*str != *prefix) {
            return false;
        }
        str++;
        prefix++;
    }

    return true;
}

static inline const char* str_skip_char(const char* str, char ch) {
    if (!str) {
        return nullptr;
    }

    while (*str == ch) {
        str++;
    }

    return str;
}