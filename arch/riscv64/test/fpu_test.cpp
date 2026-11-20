#include "test/unit/sched/register_peers_test.h"

namespace arch_trap_test {

void test_fp() {
    register_peers_test::run("FP user task isolation", "/mnt/RVFPTEST.ELF", "/mnt/RVFPPEER.ELF",
                             "User f0-f31 and fcsr survive IRQs and syscalls");
}

}  // namespace arch_trap_test
