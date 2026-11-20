#pragma once

namespace register_peers_test {

// Runs two user register fixtures from the userdata disk at once. Each fills
// the registers under test with its own values, checks them while timer
// preemption and syscalls switch between the two, and exits with 0 only if
// its state survived. `survived` names that check in the test output.
void run(const char* title, const char* first, const char* second, const char* survived);

}  // namespace register_peers_test
