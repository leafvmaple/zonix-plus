#!/usr/bin/env python3
"""Boot the production system with an xHCI controller, a USB hub with a
keyboard behind it and a USB mouse, check that all enumerate, and type a
shell command through the USB keyboard. Run after building with TEST=0."""
import argparse
import re
import time
from pathlib import Path
import tempfile

import test_console_qemu as console_test

CONSOLE_COMMAND = console_test.command

# The keyboard sits behind a hub and the mouse on a root port, so both paths
# are enumerated.
ENUMERATION_SECONDS = 3
USB_DEVICES = ["-device", "qemu-xhci,id=xhci", "-device", "usb-hub,bus=xhci.0,port=1",
               "-device", "usb-kbd,bus=xhci.0,port=1.1", "-device", "usb-mouse,bus=xhci.0,port=2"]


def without_virtio_input(arch, directory, log, rootfs=None):
    """The console command with the virtio keyboard and mouse removed, so key
    presses QEMU sends can only reach the USB keyboard."""
    command = CONSOLE_COMMAND(arch, directory, log, rootfs)
    if arch == "x86":
        # Without the PS/2 controller the USB keyboard is the only keyboard.
        return command + ["-machine", "i8042=off"]
    index = command.index("-readconfig") + 1
    text = Path(command[index]).read_text() if Path(command[index]).is_absolute() else \
        (console_test.ROOT / command[index]).read_text()
    text = re.sub(r'\[device "(keyboard|mouse)"\]\n(?:[^\[\n][^\n]*\n|\n)*', "", text)
    config = Path(directory) / "usb.cfg"
    config.write_text(text)
    command[index] = str(config)
    return command


def run(arch):
    logs = console_test.ROOT / "obj" / arch / "usb"
    logs.mkdir(parents=True, exist_ok=True)
    console_test.command = without_virtio_input
    with tempfile.TemporaryDirectory(prefix="zxusb-", dir="/tmp") as temporary:
        console = console_test.Console(arch, Path(temporary), logs / "console.log", extra=USB_DEVICES)
        try:
            console.expect("Zonix shell", "the shell to start", console_test.BOOT_TIMEOUT)
            console.prompt("the first prompt")
            # Enumeration finishes in the background; keys sent before then are lost.
            time.sleep(ENUMERATION_SECONDS)
            console.type("echo usb-typed\n")
            console.expect("usb-typed\n", "the shell echoing text typed on the USB keyboard")
            console_test.check(True, "Text typed on the USB keyboard reaches the shell")
            console.prompt("a prompt after echo")
            text = console.text()
            shell_output = text[text.find("Zonix shell"):]
            console_test.check("] [info] usb" not in shell_output,
                               "USB information arriving after boot stays off the shell's console")
            # The hub on root port 1 enumerates before the keyboard on its port 1.
            console.type("dmesg usb\n")
            console.expect("dmesg usb\n", "the shell running dmesg")
            log = console.expect("\n/ $ ", "the USB lines of the kernel log")
            console_test.check("hub with" in log and ".1: keyboard" in log and ": mouse" in log,
                               "dmesg shows the hub, the keyboard behind it and the mouse")
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
    raise SystemExit(main())
