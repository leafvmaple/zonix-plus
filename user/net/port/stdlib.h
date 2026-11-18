#pragma once

// lwIP uses this for the decimal suffix of a network interface name.
static inline int atoi(const char* text) {
    unsigned int value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10U + (unsigned int)(*text - '0');
        ++text;
    }
    return (int)value;
}
