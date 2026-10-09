"""Keep timer/preemption away from transient framebuffer cursor coordinates."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import CXX_STANDARD_FLAG, ROOT, ZSTL_FLAGS

CLANG = shutil.which("clang++")
OBJCOPY = shutil.which("llvm-objcopy")


@unittest.skipUnless(CLANG and OBJCOPY, "clang++ and llvm-objcopy are required")
class FramebufferConsoleTests(unittest.TestCase):
    def test_scroll_cursor_is_protected_from_interrupts(self):
        with tempfile.TemporaryDirectory(prefix="zonix-fbcons-tests-") as directory:
            font = Path(directory) / "font.o"
            binary = Path(directory) / "fbcons-test"
            subprocess.run([OBJCOPY, "-I", "binary", "-O", "elf64-x86-64", "-B", "i386:x86-64",
                            "--rename-section", ".data=.rodata,alloc,load,readonly,data,contents",
                            "fonts/console.psf", str(font)], cwd=ROOT, check=True, timeout=10)
            flags = [CXX_STANDARD_FLAG, *ZSTL_FLAGS, "-nostdinc", "-nostdinc++", "-ffreestanding",
                     "-fno-builtin", "-fno-exceptions", "-fno-rtti", "-ffunction-sections",
                     "-fdata-sections", "-Wl,--gc-sections", "-Werror=unused-result"]
            flags += ["-I" + str(ROOT / path) for path in ["kernel", "include", "arch/x86/include"]]
            result = subprocess.run([CLANG, *flags, str(ROOT / "kernel/test/host/fbcons_contract_test.cpp"),
                                     str(ROOT / "kernel/drivers/fbcons.cpp"), str(font), "-o", str(binary)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
