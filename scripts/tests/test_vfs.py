"""Exercise the real VFS with reentrant unmounts and filesystem failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import CXX_STANDARD_FLAG, ROOT, ZSTL_FLAGS

CLANG = shutil.which("clang++")


@unittest.skipUnless(CLANG, "clang++ is required for VFS ownership tests")
class VfsOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="zonix-vfs-ownership-")
        cls.binary = Path(cls.directory.name) / "vfs-tests"
        flags = [CXX_STANDARD_FLAG, *ZSTL_FLAGS, "-nostdinc", "-nostdinc++", "-ffreestanding",
                 "-fno-exceptions", "-fno-rtti", "-fno-elide-constructors", "-Werror=unused-result",
                 "-I" + str(ROOT / "kernel"), "-I" + str(ROOT / "include"),
                 "-I" + str(ROOT / "arch/x86/include")]
        sources = ["kernel/fs/vfs.cpp", "kernel/fs/devfs.cpp", "kernel/test/host/vfs_contract_test.cpp"]
        result = subprocess.run([CLANG, *flags, *(str(ROOT / source) for source in sources),
                                 "-o", str(cls.binary)], capture_output=True, text=True, timeout=30)
        if result.returncode:
            cls.directory.cleanup()
            raise RuntimeError("VFS ownership compilation failed:\n" + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_case(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("[OK] VFS resource contract: " + mode, result.stdout)

    def test_path_operations_and_directory_callbacks_pin_mount(self):
        self.run_case("operations")

    def test_open_transfers_pin_until_last_file_destructor(self):
        self.run_case("files")

    def test_partial_open_cleans_file_before_releasing_mount(self):
        self.run_case("partial")

    def test_success_without_file_returns_io_error(self):
        self.run_case("empty")

    def test_failed_open_preserves_errors_and_clears_output(self):
        self.run_case("failures")

    def test_mount_reserves_slot_until_failure_or_teardown_finishes(self):
        self.run_case("mount")

    def test_path_routing_and_prefix_boundaries_are_preserved(self):
        self.run_case("paths")

    def test_device_factories_propagate_errors_and_registry_visitors_use_snapshot(self):
        self.run_case("devices")


if __name__ == "__main__":
    unittest.main()
