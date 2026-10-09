"""Exercise real task creation/reaping with allocator and console failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import CXX_STANDARD_FLAG, ROOT, ZSTL_FLAGS

CLANG = shutil.which("clang++")


@unittest.skipUnless(CLANG, "clang++ is required for task ownership tests")
class TaskOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="zonix-task-ownership-")
        cls.binary = Path(cls.directory.name) / "task-tests"
        cls.flags = [CXX_STANDARD_FLAG, *ZSTL_FLAGS, "-nostdinc", "-nostdinc++", "-ffreestanding",
                     "-fno-exceptions", "-fno-rtti", "-fno-elide-constructors", "-Werror=unused-result",
                     "-ffunction-sections", "-fdata-sections", "-I" + str(ROOT / "kernel"),
                     "-I" + str(ROOT / "include"), "-I" + str(ROOT / "arch/x86/include"),
                     "-I" + str(ROOT / "arch/x86/kernel")]
        sources = ["kernel/sched/sched.cpp", "kernel/sched/sched_priority_rr.cpp", "kernel/fs/fd.cpp",
                   "kernel/test/host/task_contract_test.cpp"]
        result = subprocess.run([CLANG, *cls.flags, *(str(ROOT / source) for source in sources),
                                 "-Wl,--gc-sections", "-o", str(cls.binary)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            cls.directory.cleanup()
            raise RuntimeError("Task ownership compilation failed:\n" + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_case(self, mode, code=0):
        result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        if code == 0:
            self.assertIn("[OK] task resource contract: " + mode, result.stdout)

    def test_failed_creations_release_all_resources_without_publication(self):
        for mode in ("1", "2", "3", "4", "5", "open1", "open2", "open3"):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_publication_then_wait_reclaims_kernel_resources(self):
        self.run_case("kernel")

    def test_memory_owner_moves_to_child_and_reaps_after_files_and_stack(self):
        self.run_case("user")

    def test_borrowed_boot_stack_and_kernel_memory_are_not_freed(self):
        self.run_case("borrowed")

    def test_rejects_destruction_of_active_or_linked_tasks(self):
        for mode in ("current", "linked"):
            with self.subTest(mode=mode):
                self.run_case(mode, 77)

    def test_task_and_memory_descriptors_cannot_copy_or_move(self):
        for kind in ("Task", "MemoryDesc"):
            for statement in (f"{kind} a; {kind} b(a);", f"{kind} a; {kind} b(sys::move(a));",
                              f"{kind} a, b; b = a;", f"{kind} a, b; b = sys::move(a);"):
                with self.subTest(statement=statement):
                    result = subprocess.run([CLANG, *self.flags, "-x", "c++", "-fsyntax-only", "-"],
                                            input='#include "sched/sched.h"\nvoid misuse() { ' + statement + ' }\n',
                                            capture_output=True, text=True, timeout=10)
                    self.assertNotEqual(result.returncode, 0, result.stderr)
                    self.assertIn("deleted", result.stderr)


if __name__ == "__main__":
    unittest.main()
