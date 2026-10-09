import importlib.util
from contextlib import redirect_stdout, redirect_stderr
from io import StringIO
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "pre_commit.py"
spec = importlib.util.spec_from_file_location("pre_commit", SCRIPT)
hook = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hook)


class StagedSnapshotChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="zonix-hook-test-")
        self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name).resolve()
        self.env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        self.git("init", "-q")
        for directory in ("scripts/tests", "kernel", "docs"):
            (self.repo / directory).mkdir(parents=True)
        (self.repo / "Makefile").write_text(
            "check:\n\t@python3 scripts/check_kernel_rules.py $(ARCH)\n"
        )
        (self.repo / "scripts/check_kernel_rules.py").write_text(
            "from pathlib import Path\nimport sys\n"
            "print('checked-' + sys.argv[1])\n"
            "sys.exit(1 if 'BAD' in Path('kernel/example.cpp').read_text() else 0)\n"
        )
        (self.repo / "scripts/kernel_rule_exceptions.json").write_text("[]")
        (self.repo / "scripts/naming_exceptions.json").write_text("[]")
        (self.repo / "scripts/tests/test_naming_rules.py").write_text("")
        (self.repo / "scripts/tests/cxx_config.py").write_text("")
        (self.repo / "scripts/tests/test_cxx_config.py").write_text("")
        (self.repo / "docs/NAMING.md").write_text("")
        (self.repo / ".clang-tidy").write_text("")
        (self.repo / ".clangd").write_text("")
        (self.repo / ".clang-format").write_text("")
        (self.repo / ".vscode").mkdir()
        (self.repo / ".vscode/c_cpp_properties.json").write_text("{}")
        (self.repo / "scripts/tests/test_kernel_rules.py").write_text(
            "import unittest\nclass FixtureHarness(unittest.TestCase):\n"
            "    def test_fixture(self): self.assertTrue(True)\n"
        )
        (self.repo / "kernel/example.cpp").write_text("GOOD")

    def git(self, *args, env=None):
        return subprocess.check_output(["git", *args], cwd=self.repo, env=env or self.env)

    def check(self, index=None):
        with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
            return hook.check_index(self.repo, index)

    def test_unstaged_fix_cannot_hide_a_bad_staged_change(self):
        source = self.repo / "kernel/example.cpp"
        source.write_text("BAD")
        self.git("add", ".")
        index_before = (self.repo / ".git/index").read_bytes()
        source.write_text("GOOD")
        self.assertEqual(self.check(), 1)
        self.assertEqual(source.read_text(), "GOOD")
        self.assertEqual(self.git("show", ":kernel/example.cpp"), b"BAD")
        self.assertEqual((self.repo / ".git/index").read_bytes(), index_before)

    def test_unstaged_violation_does_not_change_a_good_staged_snapshot(self):
        self.git("add", ".")
        source = self.repo / "kernel/example.cpp"
        source.write_text("BAD")
        self.assertEqual(self.check(), 0)
        self.assertEqual(source.read_text(), "BAD")
        self.assertEqual(self.git("show", ":kernel/example.cpp"), b"GOOD")

    def test_missing_staged_harness_blocks_commit(self):
        self.git("add", "kernel/example.cpp")
        self.assertEqual(self.check(), 1)

    def test_checks_explicit_alternate_index_without_changing_main_index(self):
        self.git("add", ".")
        before = (self.repo / ".git/index").read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            index = Path(directory) / "alternate-index"
            index.write_bytes(before)
            (self.repo / "kernel/example.cpp").write_text("BAD")
            alternate_env = dict(self.env, GIT_INDEX_FILE=str(index))
            self.git("add", "kernel/example.cpp", env=alternate_env)
            self.assertEqual(self.check(index), 1)
        self.assertEqual((self.repo / ".git/index").read_bytes(), before)
        self.assertEqual(self.git("show", ":kernel/example.cpp"), b"GOOD")

    def test_document_only_change_skips_kernel_checks(self):
        (self.repo / "README.md").write_text("Documentation")
        self.git("add", "README.md")
        self.assertEqual(self.check(), 0)

    def test_naming_policy_and_configuration_changes_trigger_checks(self):
        for path in ("docs/NAMING.md", ".clang-tidy", "scripts/naming_exceptions.json",
                     "boot/example.cpp"):
            with self.subTest(path=path):
                self.assertTrue(hook.relevant(path))

    def test_missing_staged_naming_policy_blocks_commit(self):
        self.git("add", ".")
        self.git("rm", "--cached", "scripts/naming_exceptions.json")
        self.assertEqual(self.check(), 1)


if __name__ == "__main__":
    unittest.main()
