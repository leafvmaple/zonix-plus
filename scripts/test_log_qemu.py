#!/usr/bin/env python3
"""Check that the kernel log reaches /var/log/kernel.log and survives a crash.

Boots a TEST=0 system on a copy of its rootfs, which keeps what the guest
writes. The first boot checks that logd writes timestamped kernel records and a
record written to /dev/kmsg from the shell, then QEMU is killed without a
shutdown. The second boot must find the first boot's records, including that
one, before its own boot marker. Run after `make ARCH=<arch> TEST=0`.
The console logs stay under obj/<arch>/log/.
"""
import argparse
from pathlib import Path
import shutil
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_console_qemu import BOOT_TIMEOUT, KEYS, ROOT, Console, check  # noqa: E402

BOOT_MARK = "---- boot, UTC time"
# logd syncs at most this long after a record arrives; wait past it before the crash.
SYNC_WAIT = 2.5
KEYS.setdefault(">", "shift-dot")


def boot(arch, directory, log, rootfs):
    console = Console(arch, directory, log, rootfs=rootfs)
    console.expect("Zonix shell", "the shell to start", BOOT_TIMEOUT)
    console.prompt("the first prompt")
    return console


def show_log(console):
    """Prints the log file and returns its text up to the next prompt."""
    console.type("cat /var/log/kernel.log\n")
    console.expect("kernel.log\n", "the cat command line")
    return console.prompt("a prompt after cat")


def run(arch):
    logs = ROOT / "obj" / arch / "log"
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zxlog-", dir="/tmp") as temporary:
        directory = Path(temporary)
        rootfs = directory / "rootfs.img"
        shutil.copyfile(ROOT / "bin" / arch / "rootfs.img", rootfs)

        console = boot(arch, directory, logs / "first.log", rootfs)
        try:
            console.type("echo crash-test-marker > /dev/kmsg\n")
            console.prompt("a prompt after writing to /dev/kmsg")
            time.sleep(SYNC_WAIT)
            text = show_log(console)
            check(BOOT_MARK in text and "] [info] rootfs:" in text,
                  "logd writes a boot marker and timestamped kernel records")
            check("crash-test-marker" in text, "a record written to /dev/kmsg reaches the file")
        finally:
            # No shutdown and no sync: the file holds only what logd synced itself.
            console.close()

        console = boot(arch, directory, logs / "second.log", rootfs)
        try:
            time.sleep(SYNC_WAIT)
            text = show_log(console)
            first = text.find("crash-test-marker")
            second_boot = text.find(BOOT_MARK, text.find(BOOT_MARK) + 1)
            check(first >= 0 and second_boot > first,
                  "after a crash the next boot still has the previous boot's records")
        finally:
            console.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("x86", "aarch64", "riscv64"))
    args = parser.parse_args()
    try:
        run(args.arch)
    except RuntimeError as error:
        print("FAIL " + str(error), flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
