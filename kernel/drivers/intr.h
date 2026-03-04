#pragma once

#include <asm/arch.h>

namespace intr {

void enable();
void disable();

// RAII class for scoped interrupt disable
class Guard {
public:
    Guard();
    ~Guard();

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    int flag_;
};

}  // namespace intr