#include "test/unit/sched/register_peers_test.h"

namespace arch_trap_test {

void test_fp_simd() {
    register_peers_test::run("FP/SIMD user task isolation", "/mnt/FPTEST.ELF", "/mnt/FPPEER.ELF",
                             "User q0-q31, FPCR and FPSR survive IRQs and syscalls");
}

}  // namespace arch_trap_test
