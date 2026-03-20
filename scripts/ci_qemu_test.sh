#!/usr/bin/env bash
# ==========================================================================
# ci_qemu_test.sh — Boot Zonix in QEMU and verify test results
#
# Usage:
#   ./scripts/ci_qemu_test.sh [bios|uefi]
#
# Requires:
#   - QEMU (qemu-system-x86_64)
#   - Disk images already built with  make ARCH=x86 TEST=1
#   - OVMF firmware for UEFI mode
#
# Exit codes:
#   0  All tests passed
#   1  One or more tests failed, or tests did not complete
# ==========================================================================

set -euo pipefail

MODE="${1:-bios}"
BINDIR="${BINDIR:-bin/x86}"
OVMF="${OVMF:-/usr/share/ovmf/OVMF.fd}"
TIMEOUT="${TIMEOUT:-120}"
SERIAL_LOG="$(mktemp /tmp/zonix-ci-XXXXXX.log)"
QEMU="qemu-system-x86_64"

cleanup() {
    rm -f "$SERIAL_LOG"
}
trap cleanup EXIT

echo "=== Zonix CI Test Runner ==="
echo "  Mode:    $MODE"
echo "  Timeout: ${TIMEOUT}s"
echo "  Log:     $SERIAL_LOG"
echo ""

# ── Build QEMU command ──────────────────────────────────────────────────

QEMU_COMMON=(
    -display none
    -no-reboot
    -device "isa-debug-exit,iobase=0xf4,iosize=0x04"
    -serial "file:${SERIAL_LOG}"
)

if [ "$MODE" = "uefi" ]; then
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
        "$QEMU"
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
elif [ "$MODE" = "bios" ]; then
    if [ ! -f "${BINDIR}/zonix.img" ]; then
        echo "ERROR: ${BINDIR}/zonix.img not found. Run: make ARCH=x86 TEST=1"
        exit 1
    fi

    QEMU_CMD=(
        "$QEMU"
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
    echo "ERROR: Unknown mode '$MODE'. Use 'bios' or 'uefi'."
    exit 1
fi

# ── Run QEMU ────────────────────────────────────────────────────────────

echo "Starting QEMU ($MODE mode)..."
echo "  ${QEMU_CMD[*]}"
echo ""

set +e
timeout "$TIMEOUT" "${QEMU_CMD[@]}"
QEMU_EXIT=$?
set -e

# isa-debug-exit: value V → exit code (V<<1)|1
#   V=0 → exit 1   (our "success" signal)
#   timeout → exit 124
echo ""
echo "QEMU exited with code: $QEMU_EXIT"

# ── Analyze serial output ───────────────────────────────────────────────

echo ""
echo "=== Serial Output (last 80 lines) ==="
tail -n 80 "$SERIAL_LOG" 2>/dev/null || echo "(empty)"
echo "=== End Serial Output ==="
echo ""

FAIL_COUNT=0
PASS_COUNT=0

if [ -f "$SERIAL_LOG" ]; then
    FAIL_COUNT=$(grep -c '\[FAIL\]' "$SERIAL_LOG" 2>/dev/null || true)
    PASS_COUNT=$(grep -c '\[OK\]' "$SERIAL_LOG" 2>/dev/null || true)
fi

COMPLETE=0
if [ -f "$SERIAL_LOG" ] && grep -q 'CI_TEST_COMPLETE' "$SERIAL_LOG" 2>/dev/null; then
    COMPLETE=1
fi

echo "=== Results ==="
echo "  Assertions passed: $PASS_COUNT"
echo "  Assertions failed: $FAIL_COUNT"
echo "  Test suite completed: $([ $COMPLETE -eq 1 ] && echo 'yes' || echo 'NO')"

if [ "$QEMU_EXIT" -eq 124 ]; then
    echo ""
    echo "FAILURE: QEMU timed out after ${TIMEOUT}s — kernel may be hung."
    exit 1
fi

if [ "$COMPLETE" -eq 0 ]; then
    echo ""
    echo "FAILURE: CI_TEST_COMPLETE marker not found — tests did not finish."
    exit 1
fi

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo ""
    echo "FAILURE: $FAIL_COUNT test assertion(s) failed."
    grep '\[FAIL\]' "$SERIAL_LOG" 2>/dev/null | head -20
    exit 1
fi

echo ""
echo "SUCCESS: All $PASS_COUNT assertions passed."
exit 0
