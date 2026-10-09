"""Verify the shared STL dependency in hosted and strict freestanding modes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import ROOT, ZSTL_FLAGS

CLANG = shutil.which("clang++")
LIBRARY = ROOT / "external/zstl"


@unittest.skipUnless(CLANG, "clang++ is required for zstl integration tests")
class ZstlIntegrationTests(unittest.TestCase):
    def test_memory_contract_in_cxx17_and_cxx20(self):
        with tempfile.TemporaryDirectory(prefix="zonix-zstl-memory-") as directory:
            for standard in ("c++17", "c++20"):
                with self.subTest(standard=standard):
                    binary = Path(directory) / standard
                    result = subprocess.run([CLANG, "-std=" + standard, "-fno-exceptions", "-fno-rtti",
                                             "-fno-elide-constructors", "-I" + str(LIBRARY / "include"),
                                             str(LIBRARY / "tests/memory_test.cpp"), "-o", str(binary)],
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_no_system_headers_for_all_kernel_targets_and_language_versions(self):
        with tempfile.TemporaryDirectory(prefix="zonix-zstl-freestanding-") as directory:
            for standard in ("c++17", "c++20"):
                for target in ("x86_64-none-elf", "aarch64-none-elf", "riscv64-none-elf"):
                    with self.subTest(standard=standard, target=target):
                        result = subprocess.run(
                            [CLANG, "-std=" + standard, "--target=" + target, *ZSTL_FLAGS,
                             "-ffreestanding", "-nostdinc", "-nostdinc++", "-fno-exceptions", "-fno-rtti",
                             "-Werror", "-c", str(LIBRARY / "tests/freestanding_test.cpp"),
                             "-o", str(Path(directory) / (standard + target + ".o"))],
                            capture_output=True, text=True, timeout=30)
                        self.assertEqual(result.returncode, 0, result.stderr)

    def test_array_ownership_rejects_fixed_bound_factories(self):
        result = subprocess.run([CLANG, "-std=c++17", *ZSTL_FLAGS, "-nostdinc", "-nostdinc++",
                                 "-x", "c++", "-fsyntax-only", "-"],
                                input='#include <sys/memory.hpp>\nvoid misuse() { auto array = sys::make_unique<int[4]>(); }\n',
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn("deleted", result.stderr)


if __name__ == "__main__":
    unittest.main()
