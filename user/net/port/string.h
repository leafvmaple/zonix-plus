#pragma once
#include "minilib.h"

static inline int strncmp(const char* first, const char* second, __SIZE_TYPE__ size) {
    for (; size != 0; --size, ++first, ++second) {
        int difference = (unsigned char)*first - (unsigned char)*second;
        if (difference != 0 || *first == '\0')
            return difference;
    }
    return 0;
}
