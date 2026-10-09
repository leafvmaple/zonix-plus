"""Inject allocation/fork failures around the real exec and ELF loading code."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import CXX_STANDARD_FLAG, ROOT

CLANG = shutil.which("clang++")


@unittest.skipUnless(CLANG, "clang++ is required for exec contract tests")
class ExecContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="zonix-exec-tests-")
        cls.binary = Path(cls.directory.name) / "exec-tests"
        flags = [CXX_STANDARD_FLAG, "-ffreestanding", "-fno-exceptions", "-fno-rtti",
                 "-fno-elide-constructors", "-Werror=unused-result", "-Werror=unused-value",
                 "-I" + str(ROOT / "kernel"), "-I" + str(ROOT / "include"),
                 "-I" + str(ROOT / "arch/x86/include"), "-I" + str(ROOT / "arch/x86/kernel")]
        sources = ["kernel/exec/exec.cpp", "kernel/exec/elf_loader.cpp", "kernel/test/host/exec_contract_test.cpp"]
        result = subprocess.run([CLANG, *flags, *(str(ROOT / source) for source in sources), "-o", str(cls.binary)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            cls.directory.cleanup()
            raise RuntimeError("Exec contract test compilation failed:\n" + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_case(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("[OK] exec ownership and errors: " + mode, result.stdout)
        self.assertNotIn("failed", result.stdout)
        self.assertNotIn("invalid ELF", result.stdout)

    def test_success_transfers_address_space_to_child_once(self):
        self.run_case("success")

    def test_every_allocation_failure_reclaims_the_partial_image(self):
        # Buffer, MemoryDesc, root, two ELF pages and four stack pages.
        for allocation in range(1, 10):
            with self.subTest(allocation=allocation):
                self.run_case(str(allocation))

    def test_fork_failure_preserves_error_and_reclaims_complete_image(self):
        self.run_case("fork")

    def test_missing_file_returns_without_a_fault_log(self):
        self.run_case("missing")

    def test_invalid_elf_returns_without_a_fault_log(self):
        self.run_case("bad-elf")


if __name__ == "__main__":
    unittest.main()
