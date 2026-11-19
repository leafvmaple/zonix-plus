#!/usr/bin/env python3
"""Verify that running off a kernel stack stops a TEST=1 kernel with a clear panic.

A marker file makes the kernel start a thread that recurses until its stack is
exhausted. The guard page below the stack must turn that into a
"kernel stack overflow" panic naming the task. Run after building the selected
architecture's TEST=1 disk images. Only temporary disk copies are modified. The
serial log stays under obj/<arch>/stack-guard-<boot>/.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import test_shutdown_qemu as images

ROOT = images.ROOT
TIMEOUT = 240
PANIC = "kernel stack overflow in task"
# Printed by the aarch64 and riscv64 trap entries when they switch to the
# overflow stack. Without that switch the overflow is still reported, but only
# after the handler has run on the memory below the guard page. x86 cannot
# report an overflow at all without its separate double-fault stack.
OVERFLOW_STACK = "reporting from the overflow stack"


def run(arch, mode):
    logs = ROOT / "obj" / arch / f"stack-guard-{mode}"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / "overflow.log"
    log.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="zxstack-", dir="/tmp") as temporary:
        directory = Path(temporary)
        command, _, roots, offset = images.prepare(arch, mode, directory)
        marker = directory / "marker"
        marker.write_text("overflow")
        for image in roots:
            images.mtools(image, offset, marker, "::/KSTACK.RUN")
        process = subprocess.Popen(
            command + ["-display", "none", "-no-reboot", "-serial", f"file:{log}"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        deadline = time.monotonic() + TIMEOUT
        try:
            while True:
                output = log.read_text(errors="replace") if log.exists() else ""
                if PANIC in output:
                    line = next(line for line in output.splitlines() if PANIC in line)
                    if "KSTACK_OVERFLOW_BEGIN" not in output:
                        raise RuntimeError("The panic came before the overflow fixture started")
                    if arch != "x86" and OVERFLOW_STACK not in output:
                        raise RuntimeError("The overflow was reported from the exhausted stack")
                    print("PASS " + line.strip(), flush=True)
                    return
                if "KSTACK_OVERFLOW_NOT_DETECTED" in output or "CI_TEST_COMPLETE" in output:
                    raise RuntimeError("The kernel kept running after its stack overflowed")
                if process.poll() is not None:
                    raise RuntimeError(f"QEMU exited without the panic; console ends with:\n{output[-1500:]}")
                if time.monotonic() > deadline:
                    raise RuntimeError(f"No panic within {TIMEOUT}s; console ends with:\n{output[-1500:]}")
                time.sleep(0.2)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stderr.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("x86", "aarch64", "riscv64"))
    parser.add_argument("--boot", choices=("bios", "uefi"), default="uefi")
    args = parser.parse_args()
    if args.arch != "x86" and args.boot != "uefi":
        parser.error("BIOS boot is supported only for x86")
    try:
        run(args.arch, args.boot)
    except RuntimeError as error:
        print("FAIL " + str(error), flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
