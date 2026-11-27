#!/usr/bin/env python3
"""Exercise netd and the socket programs of a TEST=0 system from the host.

Boots the installed system with the network application disk built by
`make network-image` and QEMU's user network. Types commands on the console and
talks to them from host sockets: interface configuration, ping, DNS, a TCP
client, UDP in both directions, HTTP downloads with fetch and tcpserve with two
concurrent connections.
Run after `make ARCH=<arch> TEST=0` and `make ARCH=<arch> TEST=0 network-image`.
Disks are opened as snapshots. The console log stays under obj/<arch>/network/.
"""
import argparse
import http.server
import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_console_qemu import BOOT_TIMEOUT, KEYS, ROOT, Console, check  # noqa: E402

GUEST_SERVER_PORT = 7000
ECHO_BYTES = 64 * 1024
HTTP_BYTES = 256 * 1024
# Shell redirection needs "<", which the console test does not type.
KEYS.setdefault("<", "shift-comma")
KEYS.setdefault(">", "shift-dot")
STEP_TIMEOUT = 60


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def network(arch, image, forwarded):
    apps = ["-drive", f"file={image},format=raw,if=none,id=apps"]
    apps += ["-device", "ide-hd,bus=ahci0.2,drive=apps"] if arch == "x86" else \
        ["-device", "sdhci-pci", "-device", "sd-card,drive=apps"]
    # The guest reaches host services at 10.0.2.2, which needs an unrestricted user network.
    return apps + [
        "-netdev", f"user,id=net0,ipv6=off,hostfwd=tcp:127.0.0.1:{forwarded}-:{GUEST_SERVER_PORT}",
        "-device", "virtio-net-pci,disable-legacy=on,netdev=net0"]


def wait_for_text(console, wanted, what, timeout=STEP_TIMEOUT):
    deadline = time.monotonic() + timeout
    while wanted not in console.text():
        if console.process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(f"Did not see {what} ({wanted!r}). Console ends with:\n{console.text()[-1500:]}")
        time.sleep(0.2)


def run_command(console, line, wanted, what):
    console.type(line + "\n")
    console.expect(wanted, what)
    console.prompt(f"a prompt after {line!r}")


class HttpServer:
    """Serves a redirect, a chunked page and a binary file on the host loopback."""

    def __init__(self):
        self.data = os.urandom(HTTP_BYTES)
        data = self.data

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *arguments):
                pass

            def do_GET(self):
                if self.path == "/moved":
                    self.send_response(302)
                    self.send_header("Location", "/hello.txt")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif self.path == "/hello.txt":
                    self.reply(b"hello over http\n")
                elif self.path == "/chunked.txt":
                    self.send_response(200)
                    self.send_header("Transfer-Encoding", "chunked")
                    self.end_headers()
                    for part in (b"zonix ", b"chunked ", b"body end\n"):
                        self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
                    self.wfile.write(b"0\r\n\r\n")
                elif self.path == "/data.bin":
                    self.reply(data)
                else:
                    self.send_error(404)

            def reply(self, body):
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]

    def __enter__(self):
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *exception):
        self.server.shutdown()
        self.server.server_close()


def echo_through(port, payload, results, index):
    """Sends the payload, ends the sending direction and collects what comes back."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=STEP_TIMEOUT) as stream:
            sender = threading.Thread(target=lambda: (stream.sendall(payload), stream.shutdown(socket.SHUT_WR)))
            sender.start()
            received = bytearray()
            while True:
                chunk = stream.recv(65536)
                if not chunk:
                    break
                received += chunk
            sender.join()
            results[index] = bytes(received)
    except OSError as error:
        results[index] = error


def run(arch, image):
    logs = ROOT / "obj" / arch / "network"
    logs.mkdir(parents=True, exist_ok=True)
    forwarded = free_port()
    with tempfile.TemporaryDirectory(prefix="zxnet-", dir="/tmp") as temporary:
        console = Console(arch, Path(temporary), logs / "console.log", network(arch, image, forwarded))
        try:
            console.expect("init: started /opt/bin/netd", "init starting netd", BOOT_TIMEOUT)
            console.expect("Zonix shell", "the shell to start", BOOT_TIMEOUT)
            console.prompt("the first prompt")
            console.poll_command("dmesg netd", "netd: address 10.0.2.15, DNS 10.0.2.3", "the DHCP lease",
                                 STEP_TIMEOUT)

            console.type("netinfo\n")
            console.expect("address 10.0.2.15", "the interface address")
            console.expect("gateway 10.0.2.2", "the gateway")
            console.prompt("a prompt after netinfo")
            check(True, "netinfo reports the DHCP lease")

            run_command(console, "ping -c 2 10.0.2.2", "Reply from 10.0.2.2", "an echo reply")
            check(True, "ping gets an echo reply through netd")

            run_command(console, "netinfo localhost", "localhost is 127.0.0.1", "a DNS answer")
            check(True, "a host name resolves through the DHCP-supplied DNS server")

            # TCP client: the guest connects to a host listener.
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(STEP_TIMEOUT)
                console.type(f"nc 10.0.2.2 {listener.getsockname()[1]}\n")
                peer, _ = listener.accept()
                with peer:
                    peer.settimeout(STEP_TIMEOUT)
                    peer.sendall(b"from-host\n")
                    console.expect("from-host", "host bytes on the guest console")
                    console.type("from-guest\n")
                    received = b""
                    while b"from-guest" not in received:
                        received += peer.recv(100)
                console.prompt("a prompt after the host closed the connection")
            check(True, "a TCP client carries bytes both ways and ends when the peer closes")

            # UDP: the guest sends a datagram and prints the reply.
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as datagrams:
                datagrams.bind(("127.0.0.1", 0))
                datagrams.settimeout(STEP_TIMEOUT)
                console.type(f"nc -u 10.0.2.2 {datagrams.getsockname()[1]}\n")
                console.type("udp-ping\n")
                data, sender = datagrams.recvfrom(2048)
                datagrams.sendto(b"udp-pong\n", sender)
                console.expect("udp-pong", "the reply datagram")
                console.press("ctrl-c")
                console.prompt("a prompt after leaving nc")
            check(data == b"udp-ping\n", "UDP datagrams travel both ways")

            # HTTP: fetch follows a redirect, decodes chunked bodies and saves a binary file,
            # which nc then sends back so the host can compare it.
            with HttpServer() as http:
                base = f"http://10.0.2.2:{http.port}"
                run_command(console, f"fetch -o - {base}/moved", "hello over http", "the redirected page")
                run_command(console, f"fetch -o - {base}/chunked.txt", "zonix chunked body end", "the chunked body")
                console.type(f"fetch -o /tmp/data.bin {base}/data.bin\n")
                console.expect(f"saved {len(http.data)} bytes", "the saved download")
                console.prompt("a prompt after the download")
                with socket.socket() as listener:
                    listener.bind(("127.0.0.1", 0))
                    listener.listen(1)
                    listener.settimeout(STEP_TIMEOUT)
                    console.type(f"nc 10.0.2.2 {listener.getsockname()[1]} < /tmp/data.bin\n")
                    peer, _ = listener.accept()
                    with peer:
                        peer.settimeout(STEP_TIMEOUT)
                        uploaded = bytearray()
                        while chunk := peer.recv(65536):
                            uploaded += chunk
                    console.prompt("a prompt after sending the file back")
            check(bytes(uploaded) == http.data, "fetch saves a file byte for byte, after a redirect and a chunked page")

            # nc -l saves what a host sends into a file. Its own messages go to
            # the console, so the file holds the peer's bytes and nothing else.
            console.type(f"nc -l {GUEST_SERVER_PORT} > /tmp/received.bin\n")
            payload = os.urandom(ECHO_BYTES)
            deadline = time.monotonic() + STEP_TIMEOUT
            while True:
                # A connection made before nc listens is refused by the guest; try again.
                stream = socket.create_connection(("127.0.0.1", forwarded), timeout=STEP_TIMEOUT)
                try:
                    wait_for_text(console, "nc: connection from", "nc accepting the host", timeout=1)
                    break
                except RuntimeError:
                    stream.close()
                    if time.monotonic() > deadline:
                        raise
            with stream:
                stream.sendall(payload)
                stream.shutdown(socket.SHUT_WR)
                while stream.recv(65536):
                    pass
            console.prompt("a prompt after nc received a file")
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(STEP_TIMEOUT)
                console.type(f"nc 10.0.2.2 {listener.getsockname()[1]} < /tmp/received.bin\n")
                peer, _ = listener.accept()
                with peer:
                    peer.settimeout(STEP_TIMEOUT)
                    saved = bytearray()
                    while chunk := peer.recv(65536):
                        saved += chunk
                console.prompt("a prompt after sending the received file back")
            check(bytes(saved) == payload, "nc -l saves exactly the bytes it received")

            # TCP server: tcpserve runs tcpecho for two connections at once.
            console.type(f"tcpserve {GUEST_SERVER_PORT} /opt/bin/tcpecho\n")
            console.expect(f"listening on port {GUEST_SERVER_PORT}", "tcpserve listening")
            payloads = [os.urandom(ECHO_BYTES), os.urandom(ECHO_BYTES)]
            results = [None, None]
            started = time.monotonic()
            workers = [threading.Thread(target=echo_through, args=(forwarded, payloads[i], results, i))
                       for i in range(2)]
            for worker in workers:
                worker.start()
            for worker in workers:
                worker.join(STEP_TIMEOUT * 2)
            elapsed = time.monotonic() - started
            print(f"INFO echoed 2 x {ECHO_BYTES // 1024} KiB in {elapsed:.2f} s "
                  f"({2 * ECHO_BYTES / elapsed / 1024:.0f} KiB/s each way)", flush=True)
            check(results == payloads, "two concurrent connections echo 64 KiB each and close")
            console.press("ctrl-c")
            console.prompt("a prompt after stopping tcpserve")
            run_command(console, "netinfo", "address 10.0.2.15", "netd still serving")
            check(True, "netd keeps serving after its clients go away")
        finally:
            console.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("x86", "aarch64", "riscv64"))
    parser.add_argument("--image", help="the network application disk (default bin/<arch>/network.img)")
    args = parser.parse_args()
    image = args.image or str(ROOT / "bin" / args.arch / "network.img")
    try:
        run(args.arch, image)
    except RuntimeError as error:
        print("FAIL " + str(error), flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
