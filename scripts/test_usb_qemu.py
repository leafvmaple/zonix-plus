#!/usr/bin/env python3
"""Boot the production system with an xHCI controller, a USB hub with a
keyboard behind it and a USB mouse, check that all enumerate, and type a
shell command through the USB keyboard. Then, on a second boot, plug and
unplug keyboards on a root port and behind a hub while the system runs.
Run after building with TEST=0."""
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
# Hotplug starts with an empty hub; keyboards come and go through QMP.
HOTPLUG_DEVICES = ["-device", "qemu-xhci,id=xhci", "-device", "usb-hub,bus=xhci.0,port=1"]


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
            log = dmesg_usb(console)
            console_test.check("hub with" in log and ".1: keyboard" in log and ": mouse" in log,
                               "dmesg shows the hub, the keyboard behind it and the mouse")
        finally:
            console.close()


def dmesg_usb(console):
    console.type("dmesg usb\n")
    console.expect("dmesg usb\n", "the shell running dmesg")
    return console.expect("\n/ $ ", "the USB lines of the kernel log")


def run_hotplug(arch):
    logs = console_test.ROOT / "obj" / arch / "usb"
    console_test.command = without_virtio_input
    with tempfile.TemporaryDirectory(prefix="zxusb-", dir="/tmp") as temporary:
        console = console_test.Console(arch, Path(temporary), logs / "hotplug.log", extra=HOTPLUG_DEVICES)
        try:
            console.expect("Zonix shell", "the shell to start", console_test.BOOT_TIMEOUT)
            console.prompt("the first prompt")
            time.sleep(ENUMERATION_SECONDS)

            console.qmp("device_add", driver="usb-kbd", bus="xhci.0", port="2", id="root_keyboard")
            time.sleep(ENUMERATION_SECONDS)
            console.type("echo root-plugged\n")
            console.expect("root-plugged\n", "text typed on a keyboard plugged into a root port")
            console_test.check(True, "A keyboard plugged into a root port while running types into the shell")
            console.prompt("a prompt after echo")

            console.qmp("device_del", id="root_keyboard")
            time.sleep(ENUMERATION_SECONDS)
            console.qmp("device_add", driver="usb-kbd", bus="xhci.0", port="1.2", id="hub_keyboard")
            time.sleep(ENUMERATION_SECONDS)
            console.type("echo hub-plugged\n")
            console.expect("hub-plugged\n", "text typed on a keyboard plugged into a hub port")
            console_test.check(True, "A keyboard plugged into a hub port while running types into the shell")
            console.prompt("a prompt after echo")

            # A port freed by an unplug takes a device again.
            console.qmp("device_del", id="hub_keyboard")
            time.sleep(ENUMERATION_SECONDS)
            console.qmp("device_add", driver="usb-kbd", bus="xhci.0", port="2", id="root_keyboard_again")
            time.sleep(ENUMERATION_SECONDS)
            console.type("echo replugged\n")
            console.expect("replugged\n", "text typed on a keyboard plugged back into a root port")
            console_test.check(True, "A keyboard plugged back into a freed root port types into the shell")
            console.prompt("a prompt after echo")

            log = dmesg_usb(console)
            console_test.check(log.count(": removed") == 2 and ".2: keyboard" in log,
                               "dmesg shows both unplugged keyboards removed and the hub's keyboard")
        finally:
            console.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("x86", "aarch64", "riscv64"))
    args = parser.parse_args()
    try:
        run(args.arch)
        run_hotplug(args.arch)
    except RuntimeError as error:
        print("FAIL " + str(error), flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
