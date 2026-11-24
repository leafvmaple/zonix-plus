#!/usr/bin/env python3
"""Boot the production system with an xHCI controller, a USB keyboard and a
USB mouse, check that both enumerate, and type a shell command through the
USB keyboard. Run after building with TEST=0."""
import argparse
import re
from pathlib import Path
import tempfile

import test_console_qemu as console_test

CONSOLE_COMMAND = console_test.command

USB_DEVICES = ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0", "-device", "usb-mouse,bus=xhci.0"]


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
            console.expect("usb: port", "a USB device to enumerate", console_test.BOOT_TIMEOUT)
            text = console.text()
            deadline_marker = "mouse" in text and "keyboard" in text
            if not deadline_marker:
                console.expect("mouse", "the USB mouse to attach")
            console_test.check("usb: port" in console.text() and "keyboard" in console.text(),
                               "xHCI enumerates the USB keyboard and mouse")
            console.type("echo usb-typed\n")
            console.expect("usb-typed\n", "the shell echoing text typed on the USB keyboard")
            console_test.check(True, "Text typed on the USB keyboard reaches the shell")
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
