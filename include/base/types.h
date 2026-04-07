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

template<typename T, typename M>
constexpr size_t offset_of(M T::* member) {
    return reinterpret_cast<size_t>(&(static_cast<T*>(nullptr)->*member));
}

template<typename T, typename M>
inline T* to_struct(void* ptr, M T::* member) {
    return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr) - offset_of(member));
}

template<typename T, size_t N>
constexpr size_t array_size(const T (&)[N]) noexcept {
    return N;
}

inline constexpr size_t SECTOR_SIZE = 512;

template<typename T, size_t SectorBytes = SECTOR_SIZE>
struct SectorArray {
    static constexpr size_t COUNT = SectorBytes / sizeof(T);
    T entries[COUNT];
} __attribute__((packed));

#endif /* !__ASSEMBLER__ */
