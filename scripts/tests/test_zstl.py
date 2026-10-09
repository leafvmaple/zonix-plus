"""Verify self-contained zstl headers separately from executable runtime policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import ROOT, ZSTL_FLAGS

CLANG = shutil.which("clang++")
GCC = shutil.which("g++")
LIBRARY = ROOT / "external/zstl"


@unittest.skipUnless(CLANG, "clang++ is required for zstl integration tests")
class ZstlIntegrationTests(unittest.TestCase):
    def test_inline_container_and_lock_contracts(self):
        self._run_contract("container_test.cpp", "c++20", [])

    @unittest.skipUnless(GCC, "g++ is required for the alternate compiler contract")
    def test_inline_container_and_lock_contracts_with_gcc(self):
        self._run_contract("container_test.cpp", "c++20", [], compiler=GCC)

    def test_cstring_contract_without_a_mode_macro(self):
        for standard in ("c++17", "c++20"):
            with self.subTest(standard=standard):
                self._run_contract("cstring_test.cpp", standard, [])

    def test_complete_header_surface_without_system_headers_or_mode_macro(self):
        with tempfile.TemporaryDirectory(prefix="zonix-zstl-no-crt-") as directory:
            for standard in ("c++17", "c++20"):
                for target in ("x86_64-none-elf", "aarch64-none-elf", "riscv64-none-elf"):
                    with self.subTest(standard=standard, target=target):
                        result = subprocess.run(
                            [CLANG, "-std=" + standard, "--target=" + target, *ZSTL_FLAGS,
                             "-ffreestanding", "-nostdinc", "-nostdinc++", "-fno-builtin",
                             "-fno-exceptions", "-fno-rtti", "-Werror", "-c",
                             str(ROOT / "kernel/test/host/zstl_no_crt_compile_test.cpp"),
                             "-o", str(Path(directory) / (standard + target + ".o"))],
                            capture_output=True, text=True, timeout=30)
                        self.assertEqual(result.returncode, 0, result.stderr)

    def _run_contract(self, source, standard, flags, compiler=CLANG):
        with tempfile.TemporaryDirectory(prefix="zonix-zstl-contract-") as directory:
            binary = Path(directory) / "contract"
            result = subprocess.run(
                [compiler, "-std=" + standard, "-fno-exceptions", "-fno-rtti", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer", *flags,
                 "-I" + str(LIBRARY / "include"), str(LIBRARY / "tests" / source),
                 "-o", str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_inline_containers_without_system_headers_for_all_targets(self):
        with tempfile.TemporaryDirectory(prefix="zonix-zstl-inline-") as directory:
            for target in ("x86_64-none-elf", "aarch64-none-elf", "riscv64-none-elf"):
                with self.subTest(target=target):
                    result = subprocess.run(
                        [CLANG, "-std=c++20", "--target=" + target, *ZSTL_FLAGS,
                         "-ffreestanding", "-nostdinc", "-nostdinc++", "-fno-exceptions", "-fno-rtti",
                         "-Werror", "-c", str(LIBRARY / "tests/container_freestanding_test.cpp"),
                         "-o", str(Path(directory) / (target + ".o"))],
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)

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
