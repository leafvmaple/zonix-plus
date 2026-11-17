#pragma once

// Auto-test runner entry point (kernel thread).
// Runs all unit test suites, prints results, and exits QEMU.
// Only compiled when TEST=1.
int test_run_all(void* arg);

namespace exec_test {
bool ensure_mnt_mounted();
}
