"""Feed the USB Bluetooth transport packet sequences QEMU cannot produce."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import CXX_STANDARD_FLAG, ROOT, ZSTL_FLAGS

CLANG = shutil.which("clang++")


@unittest.skipUnless(CLANG, "clang++ is required for Bluetooth transport tests")
class BluetoothTransportTests(unittest.TestCase):
    def test_events_are_reassembled_across_packets_and_terminators(self):
        with tempfile.TemporaryDirectory(prefix="zonix-bluetooth-tests-") as directory:
            binary = Path(directory) / "bluetooth-test"
            flags = [CXX_STANDARD_FLAG, *ZSTL_FLAGS, "-nostdinc", "-nostdinc++", "-ffreestanding",
                     "-fno-exceptions", "-fno-rtti", "-fno-use-cxa-atexit", "-ffunction-sections",
                     "-fdata-sections", "-Wl,--gc-sections", "-Werror=unused-result"]
            flags += ["-I" + str(ROOT / path) for path in ["kernel", "include", "arch/x86/include"]]
            sources = ["kernel/test/host/bluetooth_contract_test.cpp", "kernel/drivers/usb/usb_bluetooth.cpp",
                       "kernel/drivers/bluetooth/hci.cpp"]
            result = subprocess.run([CLANG, *flags, *(str(ROOT / path) for path in sources), "-o", str(binary)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("bluetooth contract passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
