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

    def dependency(self, content):
        path = self.repo / "external/zstl"
        path.mkdir(parents=True)
        env = dict(self.env, GIT_AUTHOR_NAME="Fixture", GIT_AUTHOR_EMAIL="fixture@example.invalid",
                   GIT_COMMITTER_NAME="Fixture", GIT_COMMITTER_EMAIL="fixture@example.invalid")
        subprocess.run(["git", "init", "-q", str(path)], env=env, check=True)
        header = path / "include/sys/fixture.hpp"
        header.parent.mkdir(parents=True)
        header.write_text(content)
        subprocess.run(["git", "-C", str(path), "add", "."], env=env, check=True)
        tree = subprocess.check_output(["git", "-C", str(path), "write-tree"], env=env).decode().strip()
        revision = subprocess.check_output(["git", "-C", str(path), "commit-tree", tree, "-m", "fixture"],
                                           env=env).decode().strip()
        self.git("update-index", "--add", "--cacheinfo", "160000," + revision + ",external/zstl")
        return header

    def test_dependency_export_uses_staged_revision_not_dirty_headers(self):
        self.git("add", ".")
        header = self.dependency("GOOD")
        header.write_text("BAD")
        with tempfile.TemporaryDirectory() as directory:
            snapshot = Path(directory)
            hook.export_staged_dependencies(self.repo, snapshot, self.env)
            self.assertEqual((snapshot / "external/zstl/include/sys/fixture.hpp").read_text(), "GOOD")
        self.assertEqual(header.read_text(), "BAD")

    def test_dependency_changes_trigger_the_hook(self):
        self.assertTrue(hook.relevant("external/zstl"))
        self.assertTrue(hook.relevant(".gitmodules"))

    def test_dirty_dependency_fix_cannot_hide_a_bad_pinned_revision(self):
        checker = self.repo / "scripts/check_kernel_rules.py"
        checker.write_text(checker.read_text() +
                           "\nsys.exit(1 if 'BAD' in Path('external/zstl/include/sys/fixture.hpp').read_text() else 0)\n")
        # Remove the preceding unconditional exit in this fixture checker.
        checker.write_text(checker.read_text().replace(
            "sys.exit(1 if 'BAD' in Path('kernel/example.cpp').read_text() else 0)", ""))
        self.git("add", ".")
        header = self.dependency("BAD")
        header.write_text("GOOD")
        self.assertEqual(self.check(), 1)
        self.assertEqual(header.read_text(), "GOOD")

    def test_missing_dependency_object_blocks_the_snapshot(self):
        self.git("add", ".")
        self.git("update-index", "--add", "--cacheinfo", "160000," + "1" * 40 + ",external/zstl")
        self.assertEqual(self.check(), 1)


if __name__ == "__main__":
    unittest.main()
