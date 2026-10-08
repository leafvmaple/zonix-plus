#!/usr/bin/env python3
"""Check the exact Git index in an isolated directory before a local commit."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ARCHES = ("x86", "aarch64", "riscv64")
REQUIRED = ("scripts/check_kernel_rules.py", "scripts/kernel_rule_exceptions.json",
            "scripts/tests/test_kernel_rules.py", "Makefile")


def relevant(path):
    return path.startswith(("kernel/", "arch/", "include/", ".githooks/", "scripts/tests/")) or path in {
        "Makefile", "AGENTS.md", "scripts/check_kernel_rules.py",
        "scripts/kernel_rule_exceptions.json", "scripts/pre_commit.py",
    }


def check_index(repo, index=None):
    git_env = dict(os.environ)
    if index:
        git_env["GIT_INDEX_FILE"] = str(index)
    changed = subprocess.check_output(
        ["git", "diff", "--cached", "--name-only", "-z"], cwd=repo, env=git_env
    ).decode("utf-8", errors="surrogateescape").split("\0")
    if not any(relevant(path) for path in changed if path):
        print("Pre-commit kernel rules: no relevant staged changes", flush=True)
        return 0

    # The temporary tree has no .git directory. Drop Git's hook environment so
    # subprocesses cannot accidentally operate on the user's checkout or index.
    check_env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    with tempfile.TemporaryDirectory(prefix="zonix-pre-commit-") as directory:
        snapshot = Path(directory)
        subprocess.run(
            ["git", "checkout-index", "--all", f"--prefix={snapshot.as_posix()}/"],
            cwd=repo, env=git_env, check=True
        )
        missing = [path for path in REQUIRED if not (snapshot / path).is_file()]
        if missing:
            print("Pre-commit: required harness files are missing from the index; "
                  "stage them with the change:\n  " + "\n  ".join(missing), file=sys.stderr)
            return 1
        print("Pre-commit: checking staged snapshot (x86, aarch64, riscv64)", flush=True)
        commands = [[sys.executable, "-m", "unittest", "discover", "-s", "scripts/tests",
                     "-p", "test_*.py"]]
        commands.extend(["make", "check", f"ARCH={arch}"] for arch in ARCHES)
        for command in commands:
            result = subprocess.run(command, cwd=snapshot, env=check_env)
            if result.returncode:
                print("Pre-commit: kernel rules failed; commit blocked", file=sys.stderr)
                return 1
    print("Pre-commit kernel rules: OK", flush=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--index", type=Path)
    args = parser.parse_args()
    try:
        return check_index(args.repo.resolve(), args.index)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Pre-commit check failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
