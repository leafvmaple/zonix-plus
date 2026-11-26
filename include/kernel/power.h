#pragma once

#include "lib/result.h"

namespace power {

// Task-affine lease: moving within the preparing task transfers the freeze.
// Destruction resumes I/O, including every error path before actual power-off.
class ShutdownGuard {
public:
    ShutdownGuard(const ShutdownGuard&) = delete;
    ShutdownGuard& operator=(const ShutdownGuard&) = delete;
    ShutdownGuard(ShutdownGuard&& other) noexcept;
    ShutdownGuard& operator=(ShutdownGuard&&) = delete;
    ~ShutdownGuard();

private:
    friend Result<ShutdownGuard> prepare_shutdown();
    ShutdownGuard() = default;
    bool files_frozen_{};
    bool blocks_frozen_{};
};

Result<ShutdownGuard> prepare_shutdown();

// Drain I/O and synchronize storage before platform power-off. Success never
// returns; failure resumes storage and restores the caller's interrupt state.
// Storage that is already offline (Error::NoDevice) cannot be synchronized
// and does not stop it; any other synchronization failure does.
Error shutdown();

// Like shutdown, then resets the machine instead of removing power.
Error reboot();

}  // namespace power
