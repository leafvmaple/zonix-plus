#!/usr/bin/env python3
"""Add function names and source lines to the backtraces in a Zonix log.

Reads a console capture or /var/log/kernel.log (or standard input) and prints
it again. After each backtrace line ("... pc 0x...", "... called from 0x..." or
"  #N 0x...") comes the function and source line, looked up in the kernel ELF
of the same build, for example bin/x86/kernel. Needs llvm-symbolizer.
"""
import argparse
import re
import shutil
import subprocess
import sys

ADDRESS = re.compile(r"(\bpc|\bfrom|#\d+) (0x[0-9a-fA-F]+)\s*$")


def lookup_address(match):
    """A return address follows its call; step back into the call itself."""
    address = int(match.group(2), 16)
    return address if match.group(1) == "pc" else address - 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", help="the kernel ELF that produced the log")
    parser.add_argument("log", nargs="?", help="the log file (default standard input)")
    args = parser.parse_args()
    symbolizer = shutil.which("llvm-symbolizer") or shutil.which("llvm-symbolizer-18")
    if not symbolizer:
        sys.exit("llvm-symbolizer is required")
    text = open(args.log, errors="replace").read() if args.log else sys.stdin.read()
    lines = text.splitlines()
    wanted = []
    for line in lines:
        match = ADDRESS.search(line)
        if match:
            wanted.append(lookup_address(match))
    names = {}
    if wanted:
        result = subprocess.run([symbolizer, "--obj", args.kernel, "--functions=linkage", "--demangle",
                                 "--no-inlines", *map(hex, wanted)],
                                capture_output=True, text=True, check=True)
        # Each answer is a function line and a location line, then a blank line.
        answers = [block.splitlines() for block in result.stdout.strip().split("\n\n")]
        for address, answer in zip(wanted, answers):
            names[address] = "  ".join(part for part in answer if part)
    for line in lines:
        print(line)
        match = ADDRESS.search(line)
        if match:
            print(f"        {names.get(lookup_address(match), '?')}")


if __name__ == "__main__":
    main()
