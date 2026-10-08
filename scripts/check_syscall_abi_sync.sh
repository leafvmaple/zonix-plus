#!/usr/bin/env bash
# check_syscall_abi_sync.sh
#
# Verifies that the syscall ABI defined in zonix-plus matches the copy
# vendored inside the zcc submodule. Both files MUST agree on every
# `#define NR_*` and `#define *_FD`, otherwise a user program compiled by
# zcc will hit the wrong kernel handler at runtime.
#
# We intentionally keep two physical files (zcc must build standalone, so
# it cannot reach back into this repo's include/), and treat this script
# as the contract enforcer. Run from `make user` and from CI.

set -euo pipefail

ZONIX_HDR="include/abi/syscall.h"
ZCC_HDR="user/zcc/src/runtime/syscall.h"

if [[ ! -f "$ZONIX_HDR" ]]; then
    echo "ERROR: $ZONIX_HDR not found (run from repo root)" >&2
    exit 1
fi
if [[ ! -f "$ZCC_HDR" ]]; then
    echo "ERROR: $ZCC_HDR not found (run git submodule update --init first)" >&2
    exit 1
fi

# Extract only the lines that form the contract: `#define NR_*` and
# `#define *_FD`. Sort + normalize whitespace so cosmetic alignment
# differences (single space vs. multiple spaces after the macro name)
# don't trip the diff.
normalize() {
    grep -E '^[[:space:]]*#define[[:space:]]+(NR_|[A-Z]+_FD\b)' "$1" \
        | sed -E 's/[[:space:]]+/ /g' \
        | sort
}

diff_output=$(diff <(normalize "$ZONIX_HDR") <(normalize "$ZCC_HDR") || true)

if [[ -n "$diff_output" ]]; then
    echo "ERROR: syscall ABI mismatch between kernel and zcc runtime:" >&2
    echo "  kernel: $ZONIX_HDR" >&2
    echo "  zcc:    $ZCC_HDR" >&2
    echo "$diff_output" >&2
    echo "" >&2
    echo "Both files must define the same NR_* numbers and *_FD constants." >&2
    echo "Update both before committing." >&2
    exit 1
fi

echo "  CHECK   syscall ABI in sync ($ZONIX_HDR <-> $ZCC_HDR)"
