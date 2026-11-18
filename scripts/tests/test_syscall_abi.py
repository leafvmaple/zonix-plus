"""Regression tests for syscall ABI diagnostics and comparison."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "check_syscall_abi_sync.sh"
BASH = shutil.which("bash")


@unittest.skipUnless(BASH, "bash is required for the ABI checker")
class SyscallAbiTests(unittest.TestCase):
    def run_checker(self, root):
        for name in ("process.h", "fcntl.h", "terminal.h", "display.h"):
            for directory in ("include/abi", "user/zcc/src/runtime"):
                header = root / directory / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text("/* shared ABI fixture */\n")
        return subprocess.run([BASH, str(SCRIPT)], cwd=root, text=True,
                              capture_output=True, timeout=10)

    def test_missing_submodule_message_does_not_execute_git(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "include/abi/syscall.h"
            header.parent.mkdir(parents=True)
            header.write_text("#define NR_EXIT 1\n")
            git_dir = root / "bin"
            git_dir.mkdir()
            fake_git = git_dir / "git"
            fake_git.write_text('#!/bin/sh\nprintf executed > "$PWD/git-called"\n')
            fake_git.chmod(0o755)
            env = dict(os.environ, PATH=str(git_dir) + os.pathsep + os.environ["PATH"])
            result = subprocess.run([BASH, str(SCRIPT)], cwd=root, env=env,
                                    text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertIn("git submodule update --init", result.stderr)
            self.assertFalse((root / "git-called").exists())

    def test_matching_and_mismatching_contracts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            headers = [root / "include/abi/syscall.h", root / "user/zcc/src/runtime/syscall.h"]
            for header in headers:
                header.parent.mkdir(parents=True)
                header.write_text("#define NR_EXIT 1\n#define STDIN_FD 0\n")
            self.assertEqual(self.run_checker(root).returncode, 0)
            headers[1].write_text("#define NR_EXIT 2\n#define STDIN_FD 0\n")
            result = self.run_checker(root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("ABI mismatch", result.stderr)

    def test_new_calls_must_be_synchronized_with_the_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            kernel = root / "include/abi/syscall.h"
            runtime = root / "user/zcc/src/runtime/syscall.h"
            for header in (kernel, runtime):
                header.parent.mkdir(parents=True)
                header.write_text("#define NR_EXIT 1\n#define STDIN_FD 0\n")
            kernel.write_text(kernel.read_text() + "#define NR_SPAWN 400\n")
            self.assertEqual(self.run_checker(root).returncode, 1)
            runtime.write_text(runtime.read_text() + "#define NR_SPAWN 400\n")
            self.assertEqual(self.run_checker(root).returncode, 0)
            runtime.write_text(runtime.read_text() + "#define NR_UNKNOWN 401\n")
            self.assertEqual(self.run_checker(root).returncode, 1)


if __name__ == "__main__":
    unittest.main()
