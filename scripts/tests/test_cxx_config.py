"""Keep all C++ consumers and incremental builds on the project standard."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cxx_config import CXX_STANDARD, CXX_STANDARD_FLAG, ROOT


class CxxConfigurationTests(unittest.TestCase):
    def test_editor_and_formatter_standards_match_the_build(self):
        configurations = json.loads((ROOT / ".vscode/c_cpp_properties.json").read_text())["configurations"]
        for config in configurations:
            self.assertEqual(config["cppStandard"], CXX_STANDARD)
        self.assertIn("- " + CXX_STANDARD_FLAG, (ROOT / ".clangd").read_text())
        self.assertIn("Standard: c++20\n", (ROOT / ".clang-format").read_text())

    def test_kernel_and_boot_flags_use_the_project_standard(self):
        self.assertEqual(CXX_STANDARD, "gnu++20")
        for arch in ("x86", "aarch64", "riscv64"):
            with self.subTest(arch=arch):
                result = subprocess.check_output(
                    ["make", "--no-print-directory", "-s", "ARCH=" + arch,
                     "kernel-rule-config", "naming-boot-config"], cwd=ROOT, text=True,
                    env=dict(os.environ, MAKEFLAGS="", MFLAGS=""), timeout=15
                ).splitlines()
                for flags in result[1::3]:
                    standards = [flag for flag in flags.split() if flag.startswith("-std=")]
                    self.assertEqual(standards, [CXX_STANDARD_FLAG])

    def test_switching_standard_rebuilds_kernel_bios_and_uefi_but_unchanged_standard_does_not(self):
        with tempfile.TemporaryDirectory(prefix="zonix-cxx-config-") as directory:
            root = Path(directory)
            for name in ("Makefile", "arch/x86/Makefile", "arch/x86/boot/Makefile",
                         "arch/x86/boot/bios/Makefile", "arch/x86/boot/uefi/Makefile", "user/Makefile"):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            for name in ("kernel/probe.cpp", "arch/x86/boot/bios/bootload.cpp",
                         "arch/x86/boot/uefi/bootload.cpp", "boot/uefi_boot.cpp"):
                source = root / name
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text("int language_version() { return __cplusplus; }\n")
            # Record the command make actually invokes instead of building a full kernel.
            (root / "compiler.py").write_text(
                "from pathlib import Path\nimport sys\n"
                "args = sys.argv[1:]\n"
                "standard = next(arg for arg in args if arg.startswith('-std='))\n"
                "Path(args[args.index('-o') + 1]).write_text(standard)\n"
                "with Path('calls.log').open('a') as log: log.write(standard + '\\n')\n"
            )
            (root / "linker.py").write_text(
                "from pathlib import Path\nimport sys\n"
                "args = sys.argv[1:]\n"
                "output = next(arg[5:] for arg in args if arg.startswith('/out:'))\n"
                "objects = [arg for arg in args if arg.endswith('.o')]\n"
                "Path(output).write_text('\\n'.join(Path(obj).read_text() for obj in objects))\n"
            )

            def build(standard):
                subprocess.run(
                    ["make", "--no-print-directory", "-s", "ARCH=x86", "KSRCDIR=kernel",
                     "CXX=python3 compiler.py", "CXX_STANDARD=" + standard,
                     "UEFI_CXX=python3 compiler.py", "UEFI_LD=python3 linker.py",
                     "obj/x86/kernel/probe.o", "obj/x86/arch/x86/boot/bios/bootload.o", "bin/x86/BOOTX64.EFI"],
                    cwd=root, env=dict(os.environ, MAKEFLAGS="", MFLAGS=""),
                    check=True, capture_output=True, text=True, timeout=15
                )
                return [(root / name).read_text() for name in (
                    "obj/x86/kernel/probe.o", "obj/x86/arch/x86/boot/bios/bootload.o", "bin/x86/BOOTX64.EFI")]

            self.assertEqual(build("gnu++17"), ["-std=gnu++17", "-std=gnu++17", "-std=gnu++17\n-std=gnu++17"])
            expected = ["-std=gnu++20", "-std=gnu++20", "-std=gnu++20\n-std=gnu++20"]
            self.assertEqual(build("gnu++20"), expected)
            self.assertEqual(build("gnu++20"), expected)
            self.assertEqual((root / "calls.log").read_text().splitlines(), ["-std=gnu++17"] * 4 + ["-std=gnu++20"] * 4)


if __name__ == "__main__":
    unittest.main()
