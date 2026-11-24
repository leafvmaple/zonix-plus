#!/usr/bin/env python3
"""Verify Ctrl-C and raw mode on the physical console of a TEST=0 system.

Boots the installed system with its real init and shell, types on the emulated
keyboard and reads the serial console. Ctrl-C must end a foreground command and
a whole pipeline, and must reach an application in raw mode as input instead.
Run after `make ARCH=<arch> TEST=0`. Disks are opened as snapshots, so nothing
the guest writes is kept. The console log stays under obj/<arch>/console/.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
BOOT_TIMEOUT = 240
STEP_TIMEOUT = 90
KEYS = {" ": "spc", "\n": "ret", "/": "slash", ".": "dot", "-": "minus", "|": "shift-backslash",
        ":": "shift-semicolon"}
ESCAPES = re.compile(r"\x1b\[[0-9;?=]*[A-Za-z]")


def firmware(kind):
    return subprocess.check_output(
        ["bash", str(ROOT / "scripts/qemu_firmware.sh"), kind], text=True
    ).strip()


def command(arch, directory, log, rootfs=None):
    """The QEMU command `make qemu` uses for this architecture, with the serial console in `log`.

    With `rootfs`, that image replaces the built one and keeps what the guest
    writes, while every other disk stays a snapshot."""
    if arch == "x86":
        combined = directory / "firmware.fd"
        subprocess.run(["bash", str(ROOT / "scripts/qemu_firmware.sh"), "x86-bios", firmware("x86"),
                        str(combined)], check=True)
        disk = f"file={rootfs},snapshot=off" if rootfs else "file=bin/x86/rootfs.img"
        # qemu-uefi.cfg connects the serial port to QEMU's standard output.
        return ["qemu-system-x86_64", "-bios", str(combined), "-readconfig", "qemu-uefi.cfg",
                "-drive", f"{disk},format=raw,if=none,id=data0",
                "-device", "ide-hd,bus=ahci0.1,drive=data0"]
    config = f"qemu-uefi-{arch}.cfg"
    if rootfs:
        built = f'file = "bin/{arch}/rootfs.img"'
        text = (ROOT / config).read_text()
        if built not in text:
            raise RuntimeError(f"{config} does not name {built}")
        config = directory / "qemu.cfg"
        config.write_text(text.replace(built, f'file = "{rootfs}"\nsnapshot = "off"'))
    serial = ["-readconfig", str(config), "-serial", f"file:{log}"]
    if arch == "aarch64":
        return ["qemu-system-aarch64", "-cpu", "cortex-a72", "-bios", firmware(arch)] + serial
    variables = directory / "vars.fd"
    shutil.copyfile(firmware("riscv64-vars"), variables)
    return ["qemu-system-riscv64", "-cpu", "rv64",
            "-drive", f"if=pflash,format=raw,unit=0,file={firmware(arch)},readonly=on",
            "-drive", f"if=pflash,format=raw,unit=1,file={variables}"] + serial


class Console:
    def __init__(self, arch, directory, log, extra=(), rootfs=None):
        self.log = log
        self.position = 0
        qmp_path = directory / "qmp.sock"
        qmp_path.unlink(missing_ok=True)
        log.unlink(missing_ok=True)
        self.output = open(log, "wb") if arch == "x86" else subprocess.DEVNULL
        self.process = subprocess.Popen(
            command(arch, directory, log, rootfs) + list(extra) +
            ["-display", "none", "-no-reboot", "-snapshot", "-qmp", f"unix:{qmp_path},server=on,wait=off"],
            cwd=ROOT, stdin=subprocess.DEVNULL, stdout=self.output, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 30
        while not qmp_path.exists():
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("QEMU did not open its monitor: " + self.process.stderr.read().decode())
            time.sleep(0.05)
        self.client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.client.connect(str(qmp_path))
        self.stream = self.client.makefile("rw")
        self.stream.readline()
        self.qmp("qmp_capabilities")

    def qmp(self, execute, **arguments):
        self.stream.write(json.dumps({"execute": execute, "arguments": arguments}) + "\n")
        self.stream.flush()
        while True:
            reply = json.loads(self.stream.readline())
            if "error" in reply:
                raise RuntimeError(f"QMP {execute} failed: {reply['error']}")
            if "return" in reply:
                return

    def press(self, combination):
        keys = [{"type": "qcode", "data": name} for name in combination.split("-")]
        self.qmp("send-key", keys=keys, **{"hold-time": 60})
        time.sleep(0.2)

    def type(self, text):
        for character in text:
            self.press(KEYS.get(character, character))

    def text(self):
        """The console output so far, without escape sequences or carriage returns."""
        raw = self.log.read_bytes().decode(errors="replace") if self.log.exists() else ""
        return ESCAPES.sub("", raw).replace("\r", "")

    def expect(self, wanted, what, timeout=STEP_TIMEOUT):
        """Waits for `wanted` after everything matched so far and returns the text before it."""
        deadline = time.monotonic() + timeout
        while True:
            output = self.text()
            found = output.find(wanted, self.position)
            if found >= 0:
                skipped = output[self.position:found]
                self.position = found + len(wanted)
                return skipped
            if "kernel panic" in output.lower():
                raise RuntimeError("Guest panicked:\n" + output[-2000:])
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(f"Did not see {what} ({wanted!r}). Console ends with:\n{output[-1500:]}")
            time.sleep(0.2)

    def prompt(self, what):
        return self.expect(" $ ", what)

    def running(self, program):
        """Whether `ps` lists the program. Leaves the console at a fresh prompt."""
        self.type("ps\n")
        self.expect("NAME\n", "the process table")
        table = self.expect("/bin/ps", "ps in its own table")
        self.prompt("a prompt after ps")
        return program in table

    def close(self):
        try:
            self.client.close()
        finally:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            if self.output is not subprocess.DEVNULL:
                self.output.close()


def check(condition, message):
    if not condition:
        raise RuntimeError(message)
    print("PASS " + message, flush=True)


def run(arch):
    logs = ROOT / "obj" / arch / "console"
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zxcons-", dir="/tmp") as temporary:
        console = Console(arch, Path(temporary), logs / "console.log")
        try:
            console.expect("Zonix shell", "the shell to start", BOOT_TIMEOUT)
            console.prompt("the first prompt")

            # A foreground command: the terminal echoes the line and cat repeats it.
            console.type("cat\n")
            console.type("abc\n")
            console.expect("abc\nabc", "cat repeating its input")
            console.press("ctrl-c")
            console.expect("^C", "the shell reporting the interrupt")
            console.prompt("a prompt after the interrupt")
            check(not console.running("/bin/cat"), "Ctrl-C ends a foreground command and returns to the shell")

            console.type("cat | cat\n")
            console.type("def\n")
            console.expect("def\ndef", "the pipeline repeating its input")
            console.press("ctrl-c")
            console.expect("^C", "the shell reporting the interrupt")
            console.prompt("a prompt after the interrupt")
            check(not console.running("/bin/cat"), "Ctrl-C ends every command of a pipeline")

            # vi takes the console in raw mode, where Ctrl-C is a key it handles itself.
            console.type("vi /raw.txt\n")
            console.expect("i: insert", "vi's first screen")
            console.press("ctrl-c")
            console.expect("Interrupted; buffer kept", "vi handling Ctrl-C as a key")
            console.type("ifoo")
            console.press("esc")
            console.type(":wq\n")
            console.prompt("a prompt after leaving vi")
            console.type("cat /raw.txt\n")
            console.expect("foo", "the text typed after Ctrl-C")
            console.prompt("a prompt after cat")
            check(not console.running("/bin/vi"),
                  "Raw mode passes Ctrl-C to the application, which keeps running until it exits")
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
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
