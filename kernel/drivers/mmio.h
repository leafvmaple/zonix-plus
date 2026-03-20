#pragma once

#include <base/types.h>

namespace mmio {

inline uintptr_t addr(const volatile void* base, uintptr_t offset) {
    return reinterpret_cast<uintptr_t>(base) + offset;
}

inline uint8_t read8(uintptr_t address) {
    return *reinterpret_cast<volatile uint8_t*>(address);
}

inline uint16_t read16(uintptr_t address) {
    return *reinterpret_cast<volatile uint16_t*>(address);
}

inline uint32_t read32(uintptr_t address) {
    return *reinterpret_cast<volatile uint32_t*>(address);
}

inline uint64_t read64(uintptr_t address) {
    return *reinterpret_cast<volatile uint64_t*>(address);
}

inline uint8_t read8(uintptr_t base, uintptr_t offset) {
    return read8(base + offset);
}

inline uint16_t read16(uintptr_t base, uintptr_t offset) {
    return read16(base + offset);
}

inline uint32_t read32(uintptr_t base, uintptr_t offset) {
    return read32(base + offset);
}

inline uint64_t read64(uintptr_t base, uintptr_t offset) {
    return read64(base + offset);
}

inline uint8_t read8(const volatile void* base, uintptr_t offset) {
    return read8(addr(base, offset));
}

inline uint16_t read16(const volatile void* base, uintptr_t offset) {
    return read16(addr(base, offset));
}

inline uint32_t read32(const volatile void* base, uintptr_t offset) {
    return read32(addr(base, offset));
}

inline uint64_t read64(const volatile void* base, uintptr_t offset) {
    return read64(addr(base, offset));
}

inline void write8(uintptr_t address, uint8_t value) {
    *reinterpret_cast<volatile uint8_t*>(address) = value;
}

inline void write16(uintptr_t address, uint16_t value) {
    *reinterpret_cast<volatile uint16_t*>(address) = value;
}

inline void write32(uintptr_t address, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(address) = value;
}

inline void write64(uintptr_t address, uint64_t value) {
    *reinterpret_cast<volatile uint64_t*>(address) = value;
}

inline void write8(uintptr_t base, uintptr_t offset, uint8_t value) {
    write8(base + offset, value);
}

inline void write16(uintptr_t base, uintptr_t offset, uint16_t value) {
    write16(base + offset, value);
}

inline void write32(uintptr_t base, uintptr_t offset, uint32_t value) {
    write32(base + offset, value);
}

inline void write64(uintptr_t base, uintptr_t offset, uint64_t value) {
    write64(base + offset, value);
}

inline void write8(volatile void* base, uintptr_t offset, uint8_t value) {
    write8(addr(base, offset), value);
}

inline void write16(volatile void* base, uintptr_t offset, uint16_t value) {
    write16(addr(base, offset), value);
}

inline void write32(volatile void* base, uintptr_t offset, uint32_t value) {
    write32(addr(base, offset), value);
}

inline void write64(volatile void* base, uintptr_t offset, uint64_t value) {
    write64(addr(base, offset), value);
}

}  // namespace mmio