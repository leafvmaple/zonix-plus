#!/usr/bin/env python3
"""Verify TEST=1 kernels: sync, guest power-off, reboot persistence, and recovery.

Run after building the selected architecture's disk images. Only temporary disk
copies are modified. Serial logs remain under obj/<arch>/shutdown-<boot>/.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def firmware(kind):
    return subprocess.check_output(
        ["bash", str(ROOT / "scripts/qemu_firmware.sh"), kind], text=True
    ).strip()


def copy_image(source, directory):
    destination = directory / source.name
    shutil.copyfile(source, destination)
    return destination


def mtools(image, offset, *arguments):
    subprocess.run(
        ["mcopy", "-o", "-i", f"{image}@@{offset}", *map(str, arguments)],
        check=True, capture_output=True,
    )


def boot(command, directory, log, expected, recovery=False):
    qmp_path = directory / "qmp.sock"
    qmp_path.unlink(missing_ok=True)
    log.unlink(missing_ok=True)
    process = subprocess.Popen(
        command + ["-display", "none", "-no-reboot", "-serial", f"file:{log}",
                   "-qmp", f"unix:{qmp_path},server=on,wait=off"],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    deadline = time.monotonic() + 240
    events = []
    pending = b""
    recovered = False
    try:
        while not qmp_path.exists():
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("QEMU did not open its monitor")
            time.sleep(0.05)
        client.connect(str(qmp_path))
        client.settimeout(0.2)
        client.sendall(b'{"execute":"qmp_capabilities"}\n')
        while time.monotonic() < deadline:
            try:
                received = client.recv(65536)
                pending += received
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    if line.strip():
                        message = json.loads(line)
                        if "event" in message:
                            events.append(message)
            except socket.timeout:
                pass
            output = log.read_text(errors="replace") if log.exists() else ""
            if "[FAIL]" in output or "kernel panic" in output.lower():
                print("\n".join(line for line in output.splitlines() if "[FAIL]" in line), flush=True)
                print("\n".join(output.splitlines()[-80:]), flush=True)
                raise RuntimeError(f"Guest regression failed; see {log}")
            if recovery and "P0_2_SHUTDOWN_RECOVERED" in output and "CI_TEST_COMPLETE" in output:
                recovered = True
                client.sendall(b'{"execute":"quit"}\n')
                break
            if process.poll() is not None:
                break
        else:
            raise RuntimeError(f"Guest timed out; see {log}")
        process.wait(timeout=10)
        output = log.read_text(errors="replace")
        guest_shutdowns = [event for event in events if event["event"] == "SHUTDOWN"
                           and event.get("data", {}).get("guest") is True]
        shutdown = [event for event in guest_shutdowns if event["data"].get("reason") == "guest-shutdown"]
        if "[FAIL]" in output or "kernel panic" in output.lower():
            raise RuntimeError(f"Guest regression failed; see {log}")
        if process.returncode != 0 or expected not in output or "P0_2_POWER_OFF_REQUEST" not in output:
            raise RuntimeError(f"Incomplete guest run (exit {process.returncode}); see {log}")
        if recovery:
            if not recovered or guest_shutdowns:
                raise RuntimeError("Unsupported power-off did not recover without guest shutdown")
        elif not shutdown:
            raise RuntimeError("QMP did not observe a guest shutdown event")
        log.with_suffix(".qmp.json").write_text(json.dumps(events, indent=2) + "\n")
        print(f"PASS {log.name}: {output.count('[OK]')} assertions, "
              f"{'shell storage recovered' if recovery else 'guest shutdown confirmed'}", flush=True)
    finally:
        client.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        diagnostic = process.stderr.read().decode(errors="replace")
        process.stderr.close()
        if diagnostic:
            print(diagnostic.strip(), flush=True)


def run(arch, mode, unsupported):
    build = Path(os.environ.get("BINDIR", ROOT / "bin" / arch))
    logs = ROOT / "obj" / arch / f"shutdown-{mode}"
    logs.mkdir(parents=True, exist_ok=True)
    offset = 512 if mode == "bios" else 1024 * 1024
    with tempfile.TemporaryDirectory(prefix="zxp02-", dir="/tmp") as temporary:
        directory = Path(temporary)
        system = copy_image(build / ("zonix.img" if mode == "bios" else "zonix-uefi.img"), directory)
        command = [f"qemu-system-{'x86_64' if arch == 'x86' else arch}"]
        roots = [system]
        if arch == "x86":
            command += ["-m", "128M" if mode == "bios" else "256M"]
            if mode == "bios":
                command += ["-drive", f"file={system},format=raw,cache=writeback,if=ide,index=0",
                            "-device", "ahci,id=ahci0"]
            else:
                combined = directory / "firmware.fd"
                subprocess.run(["bash", str(ROOT / "scripts/qemu_firmware.sh"),
                                "x86-bios", firmware("x86"), str(combined)], check=True)
                command += ["-bios", str(combined), "-device", "ahci,id=ahci0", "-drive",
                            f"file={system},format=raw,cache=writeback,if=none,id=sys",
                            "-device", "ide-hd,bus=ahci0.0,drive=sys,bootindex=0"]
            if (build / "userdata.img").exists():
                data = copy_image(build / "userdata.img", directory)
                command += ["-drive", f"file={data},format=raw,cache=writeback,if=none,id=data0",
                            "-device", "ide-hd,bus=ahci0.1,drive=data0"]
        else:
            card = copy_image(build / "sdcard.img", directory)
            roots.append(card)
            command += ["-M", "virt", "-cpu", "cortex-a72" if arch == "aarch64" else "rv64", "-m", "256M"]
            if arch == "aarch64":
                command += ["-bios", firmware(arch)]
            else:
                variables = copy_image(Path(firmware("riscv64-vars")), directory)
                command += ["-drive", f"if=pflash,format=raw,unit=0,file={firmware(arch)},readonly=on",
                            "-drive", f"if=pflash,format=raw,unit=1,file={variables}"]
            command += ["-drive", f"file={system},format=raw,cache=writeback,if=none,id=sys",
                        "-device", "virtio-blk-pci,drive=sys", "-drive",
                        f"file={card},format=raw,cache=writeback,if=none,id=sdcard",
                        "-device", "sdhci-pci", "-device", "sd-card,drive=sdcard"]
        marker = directory / "mode"
        for phase in ("write", "verify"):
            marker.write_text(phase)
            for image in roots:
                mtools(image, offset, marker, "::/P02.RUN")
            boot(command, directory, logs / f"{phase}.log",
                 "P0_2_EXPLICIT_SYNC_OK" if phase == "write" else "P0_2_PERSISTENCE_OK")
        exported = directory / "state.bin"
        mtools(roots[-1], offset, "::/P02DIR/STATE.BIN", exported)
        expected = bytes((i * 29 + 7) & 255 for i in range(65536 + 37)) + b"after-sync"
        if exported.read_bytes() != expected:
            raise RuntimeError("Host verification found incorrect persisted file content")
        mtools(roots[-1], offset, "::/P02DIR/EMPTY.TXT", exported)
        if exported.read_bytes():
            raise RuntimeError("Persisted empty file has unexpected data")
        print("PASS host verification: file bytes, append, directory and empty file persisted", flush=True)
        if unsupported:
            marker.write_text("unsupported")
            mtools(system, offset, marker, "::/P02.RUN")
            # Start with an unused directory because the guest creates EMPTY.TXT exclusively.
            subprocess.run(["mdeltree", "-i", f"{system}@@{offset}", "::/P02DIR"], check=True,
                           stdout=subprocess.DEVNULL)
            boot(command + ["-machine", "pc,acpi=off"], directory, logs / "unsupported.log",
                 "P0_2_SHUTDOWN_RECOVERED", recovery=True)
            mtools(system, offset, "::/P02DIR/RECOVER.TXT", exported)
            if exported.read_bytes() != b"recovered":
                raise RuntimeError("Storage did not remain writable after unsupported power-off")
            print("PASS host verification: data written after failed power-off persisted", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("x86", "aarch64", "riscv64"))
    parser.add_argument("--boot", choices=("bios", "uefi"), default="uefi")
    parser.add_argument("--unsupported", action="store_true", help="Also verify x86 BIOS without ACPI")
    args = parser.parse_args()
    if (args.arch != "x86" and args.boot != "uefi") or (args.unsupported and (args.arch, args.boot) != ("x86", "bios")):
        parser.error("BIOS and --unsupported are supported only for x86 BIOS")
    run(args.arch, args.boot, args.unsupported)


if __name__ == "__main__":
    main()
