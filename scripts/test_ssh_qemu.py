#!/usr/bin/env python3
"""Log in to a TEST=0 system over SSH from the host and exercise the sessions.

Boots the installed system with the SSH application disk built by `make ssh-image`,
then connects with OpenSSH through QEMU's user network. Checks a login, two
concurrent sessions, closing one while the other keeps working, and logging in
again; prints round-trip and transfer timings. Run after `make ARCH=<arch> TEST=0`
and `make ARCH=<arch> TEST=0 ssh-image`, passing the same key files.
Disks are opened as snapshots. The console log stays under obj/<arch>/ssh/.
"""
import argparse
import base64
import os
from pathlib import Path
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_console_qemu import BOOT_TIMEOUT, ROOT, Console, check  # noqa: E402

GUEST_PORT = 22
LOGIN_TIMEOUT = 60
STEP_TIMEOUT = 30
ROUND_TRIPS = 20


def network(arch, port, image):
    apps = ["-drive", f"file={image},format=raw,if=none,id=apps"]
    apps += ["-device", "ide-hd,bus=ahci0.2,drive=apps"] if arch == "x86" else \
        ["-device", "sdhci-pci", "-device", "sd-card,drive=apps"]
    return apps + [
        "-object", "rng-builtin,id=entropy", "-device", "virtio-rng-pci,disable-legacy=on,rng=entropy",
        "-netdev", f"user,id=net0,ipv6=off,restrict=on,hostfwd=tcp:127.0.0.1:{port}-:{GUEST_PORT}",
        "-device", "virtio-net-pci,disable-legacy=on,netdev=net0"]


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Session:
    """An interactive OpenSSH client whose output is collected by a reader thread."""

    def __init__(self, port, identity, known_hosts):
        self.output = bytearray()
        self.position = 0
        self.lock = threading.Lock()
        self.process = subprocess.Popen(
            ["ssh", "-tt", "-p", str(port), "-i", str(identity), "-o", "IdentitiesOnly=yes",
             "-o", f"UserKnownHostsFile={known_hosts}", "-o", "StrictHostKeyChecking=yes",
             "-o", "BatchMode=yes", "-o", "ConnectTimeout=30",
             # OpenSSH 9.5+ otherwise sends keystrokes on a 20 ms grid, hiding the guest's latency.
             "-o", "ObscureKeystrokeTiming=no", "zonix@127.0.0.1"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        threading.Thread(target=self.collect, daemon=True).start()

    def collect(self):
        while True:
            data = self.process.stdout.read1(65536)
            if not data:
                return
            with self.lock:
                self.output += data

    def send(self, data):
        self.process.stdin.write(data if isinstance(data, bytes) else data.encode())
        self.process.stdin.flush()

    def expect(self, wanted, what, timeout=STEP_TIMEOUT):
        """Waits for `wanted` after everything matched so far; returns the bytes before it."""
        wanted = wanted.encode() if isinstance(wanted, str) else wanted
        deadline = time.monotonic() + timeout
        while True:
            with self.lock:
                found = self.output.find(wanted, self.position)
                if found >= 0:
                    skipped = bytes(self.output[self.position:found])
                    self.position = found + len(wanted)
                    return skipped
                tail = bytes(self.output[-800:])
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(f"SSH session did not show {what} ({wanted!r}). It ends with:\n"
                                   + tail.decode(errors="replace"))
            time.sleep(0.002)

    def prompt(self, what):
        return self.expect(" $ ", what)

    def run(self, line):
        """Runs a shell line and returns what it printed before the next prompt."""
        self.send(line + "\n")
        self.expect(line, "the echoed command line")
        self.expect("\n", "the end of the command line")
        return self.prompt(f"a prompt after {line!r}")

    def finish(self, timeout=STEP_TIMEOUT):
        self.send("exit\n")
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise RuntimeError("The SSH client did not end after exit")


def known_hosts_line(port, host_key):
    public = Path(host_key).read_bytes()
    if len(public) != 32:
        raise RuntimeError(f"{host_key} is not a TinySSH Ed25519 public key")
    blob = b"\x00\x00\x00\x0bssh-ed25519\x00\x00\x00\x20" + public
    return f"[127.0.0.1]:{port} ssh-ed25519 {base64.b64encode(blob).decode()}\n"


def login(port, identity, known_hosts):
    deadline = time.monotonic() + LOGIN_TIMEOUT
    while True:
        started = time.monotonic()
        session = Session(port, identity, known_hosts)
        try:
            session.prompt("the remote shell prompt")
            return session, time.monotonic() - started
        except RuntimeError:
            session.process.kill()
            session.process.wait()
            if time.monotonic() > deadline:
                raise
            time.sleep(1)


def measure(session):
    # The shell's line editor redraws the line after every key, ending with "erase to end of line".
    line = "echo " + "abcdefghijklmnopqrst"[:ROUND_TRIPS]
    rounds = []
    for index in range(len(line)):
        started = time.monotonic()
        session.send(line[index])
        session.expect(line[:index + 1] + "\x1b[K", "the echoed key")
        rounds.append(time.monotonic() - started)
    session.send("\n")
    session.prompt("a prompt after echo")
    print(f"INFO key echo: median {statistics.median(rounds) * 1000:.1f} ms, "
          f"max {max(rounds) * 1000:.1f} ms", flush=True)

    started = time.monotonic()
    output = session.run("cat /opt/bin/sshd")
    elapsed = time.monotonic() - started
    print(f"INFO guest to host: {len(output)} bytes in {elapsed:.2f} s "
          f"({len(output) / elapsed / 1024:.0f} KiB/s)", flush=True)

    lines = 512
    payload = b"".join(b"%05d" % i + b"x" * 58 + b"\n" for i in range(lines))
    session.send("cat > /tmp/upload.txt\n")
    session.expect("cat > /tmp/upload.txt", "the echoed upload command")
    started = time.monotonic()
    session.send(payload)
    session.expect(b"%05d" % (lines - 1), "the last uploaded line", 120)
    session.send(b"\x04")
    session.prompt("a prompt after the upload")
    elapsed = time.monotonic() - started
    print(f"INFO host to guest: {len(payload)} bytes in {elapsed:.2f} s "
          f"({len(payload) / elapsed / 1024:.0f} KiB/s)", flush=True)
    check(f"{lines - 1:05d}".encode() in session.run("cat /tmp/upload.txt"),
          "an upload through the terminal reaches the file")


def wait_for_text(console, wanted, what):
    deadline = time.monotonic() + LOGIN_TIMEOUT
    while wanted not in console.text():
        if console.process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(f"Did not see {what} ({wanted!r}). Console ends with:\n{console.text()[-1500:]}")
        time.sleep(0.2)


def run(arch, image, identity, host_key):
    logs = ROOT / "obj" / arch / "ssh"
    logs.mkdir(parents=True, exist_ok=True)
    port = free_port()
    with tempfile.TemporaryDirectory(prefix="zxssh-", dir="/tmp") as temporary:
        known_hosts = Path(temporary) / "known_hosts"
        known_hosts.write_text(known_hosts_line(port, host_key))
        console = Console(arch, Path(temporary), logs / "console.log", network(arch, port, image))
        sessions = []
        try:
            console.expect("init: started /opt/bin/tcpserve", "init starting the SSH service", BOOT_TIMEOUT)
            console.expect("Zonix shell", "the shell to start", BOOT_TIMEOUT)
            console.prompt("the first prompt")
            # QEMU's user network hands out 10.0.2.15 over DHCP.
            wait_for_text(console, "netd: address 10.0.2.15", "the DHCP address")
            wait_for_text(console, f"tcpserve: listening on port {GUEST_PORT}", "the SSH listener")
            first, elapsed = login(port, identity, known_hosts)
            sessions.append(first)
            print(f"INFO first login took {elapsed:.2f} s", flush=True)
            check(b"one" in first.run("echo one"), "a login runs shell commands")
            measure(first)

            second, elapsed = login(port, identity, known_hosts)
            sessions.append(second)
            print(f"INFO second login took {elapsed:.2f} s", flush=True)
            check(b"two" in second.run("echo two") and
                  b"three" in first.run("echo three"),
                  "two sessions work at the same time")

            check(second.finish() == 0, "exit ends a session")
            sessions.remove(second)
            check(b"four" in first.run("echo four"),
                  "the other session keeps working after one ends")
            check(first.finish() == 0, "the last session ends")
            sessions.remove(first)

            again, _ = login(port, identity, known_hosts)
            sessions.append(again)
            check(b"five" in again.run("echo five"), "logging in again works")
        finally:
            for session in sessions:
                if session.process.poll() is None:
                    session.process.kill()
                    session.process.wait()
            console.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("x86", "aarch64", "riscv64"))
    parser.add_argument("--identity", default=os.path.expanduser("~/.ssh/id_ed25519"),
                        help="the private key matching SSH_AUTHORIZED_KEY")
    parser.add_argument("--host-key", required=True, help="ed25519.pk in SSH_HOSTKEY_DIR")
    parser.add_argument("--image", help="the SSH application disk (default bin/<arch>/ssh.img)")
    args = parser.parse_args()
    image = args.image or str(ROOT / "bin" / args.arch / "ssh.img")
    try:
        run(args.arch, image, args.identity, args.host_key)
    except RuntimeError as error:
        print("FAIL " + str(error), flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
