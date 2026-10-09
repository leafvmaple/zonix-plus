"""Link real kernel scripts and check bootstrap memory ownership boundaries."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import ROOT

CLANG = shutil.which("clang")
LINKER = shutil.which("ld.lld")
NM = shutil.which("llvm-nm")


@unittest.skipUnless(CLANG and LINKER and NM, "LLVM tools are required for linker tests")
class KernelLinkerTests(unittest.TestCase):
    def test_riscv_small_globals_are_inside_bss_clear_and_kernel_reservation(self):
        source = """
.section .text.boot,"ax"
.globl _start
_start:
    ret
.section .rodata,"a"
    .quad __bss_start, __bss_end, KERNEL_END
.section .data.pgdir,"aw"
    .balign 4096
    .zero 4096
.section .sdata,"aw"
.globl small_initialized
small_initialized:
    .quad 42
.section .sdata.extra,"aw"
.globl small_initialized_extra
small_initialized_extra:
    .quad 43
.section .bss,"aw",@nobits
    .balign 4096
    .zero 4095
.section .sbss,"aw",@nobits
    .balign 8
.globl small_zero
small_zero:
    .zero 32
.section .sbss.extra,"aw",@nobits
.globl small_zero_extra
small_zero_extra:
    .zero 8
"""
        with tempfile.TemporaryDirectory(prefix="zonix-linker-tests-") as directory:
            obj = Path(directory) / "small-data.o"
            elf = Path(directory) / "kernel"
            subprocess.run([CLANG, "--target=riscv64-none-elf", "-x", "assembler", "-c", "-", "-o", str(obj)],
                           input=source, text=True, capture_output=True, check=True, timeout=15)
            subprocess.run([LINKER, "-T", str(ROOT / "scripts/kernel-riscv64.ld"), str(obj), "-o", str(elf)],
                           capture_output=True, text=True, check=True, timeout=15)
            symbols = {}
            for line in subprocess.check_output([NM, str(elf)], text=True, timeout=10).splitlines():
                address, _, name = line.split()
                symbols[name] = int(address, 16)
            for name, size in [("small_zero", 32), ("small_zero_extra", 8)]:
                self.assertGreaterEqual(symbols[name], symbols["__bss_start"])
                self.assertLessEqual(symbols[name] + size, symbols["__bss_end"])
                self.assertLessEqual(symbols[name] + size, symbols["KERNEL_END"])
            for name in ["small_initialized", "small_initialized_extra"]:
                self.assertLess(symbols[name], symbols["__bss_start"])


if __name__ == "__main__":
    unittest.main()
