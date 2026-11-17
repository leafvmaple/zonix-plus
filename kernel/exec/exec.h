#pragma once

#include <base/types.h>
#include <asm/page.h>
#include "lib/result.h"
#include "mm/pmm.h"
#include <sys/array.hpp>
#include <sys/inplace_vector.hpp>

struct MemoryDesc;

namespace exec {

inline constexpr size_t MAX_BINARY_SIZE = 1024ULL * 1024ULL;
inline constexpr size_t MAX_ARGS = 16;
inline constexpr size_t MAX_ARG_BYTES = 4096;
inline constexpr size_t MAX_PATH = 128;

// Materialized strings own their bytes before a task or user stack is published.
class Arguments {
public:
    Error add(const char* text);
    size_t size() const { return offsets_.size(); }
    size_t byte_count() const { return used_; }
    const char* data() const { return bytes_.data(); }
    size_t offset(size_t index) const { return offsets_[index]; }

private:
    sys::array<char, MAX_ARG_BYTES> bytes_{};
    sys::inplace_vector<size_t, MAX_ARGS> offsets_{};
    size_t used_{};
};

Result<pde_t*> create_user_pgdir();
Result<uintptr_t> setup_user_stack(MemoryDesc& memory, const Arguments& args);
Result<int> spawn(const char* path, const Arguments& args);
Result<int> exec(const char* path);

}  // namespace exec
