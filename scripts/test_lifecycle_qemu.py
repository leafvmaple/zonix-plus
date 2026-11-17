#!/usr/bin/env python3
"""Run only the P1-1 user lifecycle scenarios on disposable QEMU images."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("x86", "aarch64", "riscv64"))
    parser.add_argument("--boot", choices=("bios", "uefi"), default="uefi")
    args = parser.parse_args()
    if args.arch != "x86" and args.boot != "uefi":
        parser.error("BIOS is supported only on x86")
    source = Path(os.environ.get("BINDIR", f"bin/{args.arch}"))
    names = ["zonix.img" if args.boot == "bios" else "zonix-uefi.img"]
    if args.arch != "x86":
        names.append("sdcard.img")
    command = (["bash", "scripts/ci_qemu_test.sh", args.boot] if args.arch == "x86"
               else ["bash", f"scripts/ci_qemu_test_{args.arch}.sh"])
    with tempfile.TemporaryDirectory(prefix=f"zonix-lifecycle-{args.arch}-") as directory:
        marker = Path(directory) / "marker"
        marker.write_text("lifecycle\n")
        for name in names:
            destination = Path(directory) / name
            shutil.copyfile(source / name, destination)
            if name != "userdata.img":
                offset = 512 if args.boot == "bios" else 1024 * 1024
                subprocess.run(["mcopy", "-o", "-i", f"{destination}@@{offset}",
                                str(marker), "::/P1.RUN"], check=True)
        result = subprocess.run(command, env=dict(os.environ, BINDIR=directory))
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
