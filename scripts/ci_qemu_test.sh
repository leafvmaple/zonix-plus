#!/usr/bin/env bash
# ==========================================================================
# ci_qemu_test.sh — Boot Zonix in qemu and verify test results
#
# Usage:
#   ./scripts/ci_qemu_test.sh [bios|uefi]
#
# Requires:
#   - qemu (qemu-system-x86_64)
#   - Disk images already built with  make ARCH=x86 TEST=1
#   - OVMF firmware for UEFI mode
#
# Exit codes:
#   0  All tests passed
#   1  One or more tests failed, or tests did not complete
# ==========================================================================

set -euo pipefail

mode="${1:-bios}"
BINDIR="${BINDIR:-bin/x86}"
OVMF="${OVMF:-/usr/share/ovmf/OVMF.fd}"
TIMEOUT="${TIMEOUT:-120}"
serial_log="$(mktemp /tmp/zonix-ci-XXXXXX.log)"
qemu="qemu-system-x86_64"

cleanup() {
    rm -f "$serial_log"
}
trap cleanup EXIT

echo "=== Zonix CI Test Runner ==="
echo "  Mode:    $mode"
echo "  Timeout: ${TIMEOUT}s"
echo "  Log:     $serial_log"
echo ""

# ── Build qemu command ──────────────────────────────────────────────────

QEMU_COMMON=(
    -display none
    -no-reboot
    -device "isa-debug-exit,iobase=0xf4,iosize=0x04"
    -serial "file:${serial_log}"
)

if [ "$mode" = "uefi" ]; then
    if [ ! -f "$OVMF" ]; then
        echo "ERROR: OVMF firmware not found at $OVMF"
        echo "       Install ovmf package or set OVMF= env var"
        exit 1
    fi
    if [ ! -f "${BINDIR}/zonix-uefi.img" ]; then
        echo "ERROR: ${BINDIR}/zonix-uefi.img not found. Run: make ARCH=x86 TEST=1"
        exit 1
    fi

    QEMU_CMD=(
        "$qemu"
        -bios "$OVMF"
        -m 256M
        -device ahci,id=ahci0
        -drive "file=${BINDIR}/zonix-uefi.img,format=raw,if=none,id=sys"
        -device "ide-hd,bus=ahci0.0,drive=sys,bootindex=0"
        "${QEMU_COMMON[@]}"
    )

    # Attach userdata disk if it exists
    if [ -f "${BINDIR}/userdata.img" ]; then
        QEMU_CMD+=(
            -drive "file=${BINDIR}/userdata.img,format=raw,if=none,id=data0"
            -device "ide-hd,bus=ahci0.1,drive=data0"
        )
    fi
elif [ "$mode" = "bios" ]; then
    if [ ! -f "${BINDIR}/zonix.img" ]; then
        echo "ERROR: ${BINDIR}/zonix.img not found. Run: make ARCH=x86 TEST=1"
        exit 1
    fi

    QEMU_CMD=(
        "$qemu"
        -m 128M
        -drive "file=${BINDIR}/zonix.img,format=raw,if=ide,index=0,media=disk"
        -device ahci,id=ahci0
        "${QEMU_COMMON[@]}"
    )

    if [ -f "${BINDIR}/userdata.img" ]; then
        QEMU_CMD+=(
            -drive "file=${BINDIR}/userdata.img,format=raw,if=none,id=data0"
            -device "ide-hd,bus=ahci0.1,drive=data0"
        )
    fi
else
    echo "ERROR: Unknown mode '$mode'. Use 'bios' or 'uefi'."
    exit 1
fi

# ── Run qemu ────────────────────────────────────────────────────────────

echo "Starting qemu ($mode mode)..."
echo "  ${QEMU_CMD[*]}"
echo ""

set +e
timeout "$TIMEOUT" "${QEMU_CMD[@]}"
qemu_exit_code=$?
set -e

# isa-debug-exit: value V → exit code (V<<1)|1
#   V=0 → exit 1   (our "success" signal)
#   timeout → exit 124
echo ""
echo "qemu exited with code: $qemu_exit_code"

# ── Analyze serial output ───────────────────────────────────────────────

echo ""
echo "=== Serial Output (last 80 lines) ==="
tail -n 80 "$serial_log" 2>/dev/null || echo "(empty)"
echo "=== End Serial Output ==="
echo ""

fail_count=0
pass_count=0

if [ -f "$serial_log" ]; then
    fail_count=$(grep -c '\[FAIL\]' "$serial_log" 2>/dev/null || true)
    pass_count=$(grep -c '\[OK\]' "$serial_log" 2>/dev/null || true)
fi

complete=0
if [ -f "$serial_log" ] && grep -q 'CI_TEST_COMPLETE' "$serial_log" 2>/dev/null; then
    complete=1
fi

echo "=== Results ==="
echo "  Assertions passed: $pass_count"
echo "  Assertions failed: $fail_count"
echo "  Test suite completed: $([ $complete -eq 1 ] && echo 'yes' || echo 'NO')"

if [ "$qemu_exit_code" -eq 124 ]; then
    echo ""
    echo "FAILURE: qemu timed out after ${TIMEOUT}s — kernel may be hung."
    exit 1
fi

if [ "$complete" -eq 0 ]; then
    echo ""
    echo "FAILURE: CI_TEST_COMPLETE marker not found — tests did not finish."
    exit 1
fi

if [ "$fail_count" -gt 0 ]; then
    echo ""
    echo "FAILURE: $fail_count test assertion(s) failed."
    grep '\[FAIL\]' "$serial_log" 2>/dev/null | head -20
    exit 1
fi

echo ""
echo "SUCCESS: All $pass_count assertions passed."
exit 0
