#pragma once
#include <base/types.h>
namespace storage_host {
void arch_invalidate_tlb_range(uintptr_t va, size_t byte_count);
uint8_t arch_port_read8(uint16_t port);
void arch_port_write8(uint16_t port, uint8_t value);
void arch_port_read16_buffer(uint16_t port, void* buffer, size_t count);
void arch_port_write16_buffer(uint16_t port, void* buffer, size_t count);
}  // namespace storage_host
inline void arch_spin_hint() {}
inline void arch_io_wait() {}
inline void arch_invalidate_tlb_range(uintptr_t va, size_t byte_count) {
    storage_host::arch_invalidate_tlb_range(va, byte_count);
}
inline uint8_t arch_port_read8(uint16_t port) {
    return storage_host::arch_port_read8(port);
}
inline void arch_port_write8(uint16_t port, uint8_t value) {
    storage_host::arch_port_write8(port, value);
}
inline void arch_port_read16_buffer(uint16_t port, void* buffer, size_t count) {
    storage_host::arch_port_read16_buffer(port, buffer, count);
}
inline void arch_port_write16_buffer(uint16_t port, void* buffer, size_t count) {
    storage_host::arch_port_write16_buffer(port, buffer, count);
}
