"""Exercise real driver probes against host-only MMIO/PIO fault injection."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import CXX_STANDARD_FLAG, ROOT, ZSTL_FLAGS

CLANG = shutil.which("clang++")


@unittest.skipUnless(CLANG, "clang++ is required for storage driver tests")
class StorageDriverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="zonix-storage-tests-")
        cls.binary = Path(cls.directory.name) / "storage-tests"
        flags = [CXX_STANDARD_FLAG, *ZSTL_FLAGS, "-nostdinc", "-nostdinc++", "-ffreestanding",
                 "-fno-exceptions", "-fno-rtti", "-O2", "-ffunction-sections", "-fdata-sections",
                 "-Werror=unused-result", "-Werror=unused-value", "-pthread", "-Wl,--gc-sections"]
        includes = ["arch/x86/test/host/storage_include", "kernel", "include",
                    "arch/x86/include", "arch/x86/kernel"]
        flags += ["-I" + str(ROOT / path) for path in includes]
        sources = ["arch/x86/test/host/storage_driver_contract_test.cpp", "kernel/mm/mmio.cpp",
                   "kernel/block/blk.cpp", "kernel/drivers/pci.cpp", "kernel/drivers/sdhci.cpp",
                   "arch/x86/kernel/drivers/ahci.cpp", "arch/x86/kernel/drivers/ide.cpp"]
        result = subprocess.run([CLANG, *flags, *(str(ROOT / path) for path in sources), "-o", str(cls.binary)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            cls.directory.cleanup()
            raise RuntimeError("Storage test compilation failed:\n" + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("storage contract passed", result.stdout)

    def test_mmio_owner_and_mapping_failure(self):
        self.run_case("mmio")

    def test_rejects_copying_mapping_and_owner(self):
        flags = [CXX_STANDARD_FLAG, *ZSTL_FLAGS, "-nostdinc", "-nostdinc++", "-ffreestanding"]
        flags += ["-I" + str(ROOT / path) for path in ["kernel", "include", "arch/x86/include"]]
        for body in ["vmm::MmioRegion owner; auto copied = owner;",
                     "vmm::MmioRegion owner; auto copied = *owner;"]:
            with self.subTest(body=body):
                result = subprocess.run([CLANG, *flags, "-x", "c++", "-fsyntax-only", "-"],
                                        input='#include "mm/mmio.h"\nvoid misuse() { ' + body + ' }\n',
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stderr)

    def test_registry_batch_is_atomic(self):
        self.run_case("registry")

    def test_pci_command_restore_preserves_status(self):
        self.run_case("pci")

    def test_ahci_probe_failure_and_retry(self):
        for case in ["ahci_map", "ahci_version", "ahci_cr", "ahci_fr", "ahci_dma_stop", "ahci_io", "ahci_timeout",
                     "ahci_full", "ahci_retry", "ahci_success", "ahci_transfer_io", "ahci_transfer_timeout",
                     "ahci_issue_timeout", "ahci_transfer_quarantine"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_sdhci_probe_failure_and_retry(self):
        for case in ["sd_map", "sd_reset", "sd_clock", "sd_io", "sd_full", "sd_retry", "sd_success"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_ide_errors_interrupt_cleanup_and_repeat_init(self):
        for case in ["ide_io", "ide_timeout", "ide_write_io", "ide_write_timeout", "ide_full", "ide_success",
                     "ide_identify_timeout"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_complete_ahci_transactions_and_independent_ports(self):
        for case in ["ahci_concurrent", "ahci_independent"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_offline_devices_never_touch_hardware(self):
        self.run_case("default_offline")

    def test_initialization_cleanup_and_explicit_retry(self):
        for case in ["ahci_lifecycle", "sd_lifecycle"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_sdhci_retains_published_slots_after_io_failure(self):
        self.run_case("sd_slots")

    def test_complete_sdhci_transactions_and_independent_controllers(self):
        for case in ["sd_concurrent", "sd_independent"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_ide_master_slave_share_channel_but_channels_are_independent(self):
        for case in ["ide_same", "ide_concurrent", "ide_independent"]:
            with self.subTest(case=case):
                self.run_case(case)

    def test_ide_skips_only_aborted_identify_with_fresh_atapi_signature(self):
        for case in ["ide_atapi", "ide_atapi_io", "ide_atapi_df", "ide_atapi_timeout", "ide_atapi_ata_io",
                     "ide_identify_abrt", "ide_identify_stale"]:
            with self.subTest(case=case):
                self.run_case(case)


if __name__ == "__main__":
    unittest.main()
