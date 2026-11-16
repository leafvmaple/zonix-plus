#pragma once

#ifndef __ASSEMBLER__

using int8_t = __INT8_TYPE__;
using uint8_t = __UINT8_TYPE__;
using int16_t = __INT16_TYPE__;
using uint16_t = __UINT16_TYPE__;
using int32_t = __INT32_TYPE__;
using uint32_t = __UINT32_TYPE__;
using int64_t = __INT64_TYPE__;
using uint64_t = __UINT64_TYPE__;

using intptr_t = __INTPTR_TYPE__;
using uintptr_t = __UINTPTR_TYPE__;

using size_t = __SIZE_TYPE__;

inline constexpr size_t SECTOR_SIZE = 512;

template<typename T, size_t SECTOR_BYTES = SECTOR_SIZE>
struct SectorArray {
    static constexpr size_t COUNT = SECTOR_BYTES / sizeof(T);
    T entries[COUNT];
} __attribute__((packed));

#endif /* !__ASSEMBLER__ */
