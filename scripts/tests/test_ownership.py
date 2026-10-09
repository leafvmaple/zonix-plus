"""Exercise unique ownership, buffer lifetimes and descriptor transfers."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import ZSTL_FLAGS, CXX_STANDARD_FLAG, ROOT

CLANG = shutil.which("clang++")


@unittest.skipUnless(CLANG, "clang++ is required for ownership contract tests")
class OwnershipContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="zonix-ownership-tests-")
        cls.binary = Path(cls.directory.name) / "ownership-tests"
        cls.flags = [CXX_STANDARD_FLAG, *ZSTL_FLAGS, "-nostdinc", "-nostdinc++", "-ffreestanding", "-fno-exceptions", "-fno-rtti",
                     "-fno-elide-constructors", "-Werror=unused-result", "-Werror=unused-value",
                     "-I" + str(ROOT / "kernel"), "-I" + str(ROOT / "include"),
                     "-I" + str(ROOT / "arch/x86/include"), "-I" + str(ROOT / "arch/x86/kernel")]
        sources = ["kernel/test/unit/lib/ownership_test.cpp", "kernel/test/host/ownership_contract_test.cpp",
                   "kernel/fs/fd.cpp"]
        result = subprocess.run([CLANG, *cls.flags, *(str(ROOT / source) for source in sources), "-o", str(cls.binary)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            cls.directory.cleanup()
            raise RuntimeError("Ownership test compilation failed:\n" + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_moves_cleanup_handoff_and_descriptor_ownership(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("[FAIL]", result.stdout)
        self.assertIn("All Resource Ownership tests passed", result.stdout)

    def test_allocation_failure_has_no_live_resource(self):
        result = subprocess.run([str(self.binary), "failure"], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("failed allocation creates no owner", result.stdout)

    def test_rejects_copying_and_borrowing_temporary_owners(self):
        statements = ["sys::unique_ptr<int> owner; auto copy = owner;",
                      "KernelBuffer owner; auto copy = owner;",
                      "vfs::FileHandle owner; auto copy = owner;",
                      "fd::Table owner; auto copy = owner;",
                      "auto* ptr = KernelBuffer{}.data(); (void)ptr;",
                      "sys::unique_ptr<int> owner; owner.release();",
                      "KernelBuffer::alloc(16);"]
        for statement in statements:
            with self.subTest(statement=statement):
                result = subprocess.run([CLANG, *self.flags, "-x", "c++", "-fsyntax-only", "-"],
                                        input='#include "lib/kernel_buffer.h"\n#include "fs/fd.h"\n'
                                              'void misuse() { ' + statement + ' }\n',
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stderr)



if __name__ == "__main__":
    unittest.main()
