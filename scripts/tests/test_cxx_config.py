"""Keep the kernel and boot builds freestanding and on the project C++ standard."""
import os
import subprocess
import unittest

from cxx_config import CXX_STANDARD, CXX_STANDARD_FLAG, ROOT


class CxxConfigurationTests(unittest.TestCase):
    def test_kernel_and_boot_flags_use_the_project_standard(self):
        self.assertEqual(CXX_STANDARD, "gnu++20")
        for arch in ("x86", "aarch64", "riscv64"):
            with self.subTest(arch=arch):
                result = subprocess.check_output(
                    ["make", "--no-print-directory", "-s", "ARCH=" + arch,
                     "kernel-rule-config", "naming-boot-config"], cwd=ROOT, text=True,
                    env=dict(os.environ, MAKEFLAGS="", MFLAGS=""), timeout=15
                ).splitlines()
                self.assertIn("-Werror=unused-result", result[1].split())
                self.assertIn("-ffreestanding", result[1].split())
                self.assertIn("-nostdinc", result[1].split())
                self.assertIn("-nostdinc++", result[1].split())
                self.assertNotIn("-DZSTL_FREESTANDING", result[1].split())
                self.assertNotIn("-DZSTL_RUNTIME_DECLARATIONS_PROVIDED", result[1].split())
                self.assertIn("-Iexternal/zstl/include", result[1].split())
                for flags in result[1::3]:
                    standards = [flag for flag in flags.split() if flag.startswith("-std=")]
                    self.assertEqual(standards, [CXX_STANDARD_FLAG])


if __name__ == "__main__":
    unittest.main()
