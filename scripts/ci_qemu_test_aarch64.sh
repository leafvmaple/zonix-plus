#!/usr/bin/env bash
# ==========================================================================
# ci_qemu_test_aarch64.sh — Boot Zonix aarch64 in QEMU and verify test logs
#
# Usage:
#   ./scripts/ci_qemu_test_aarch64.sh
#
# Requires:
#   - qemu-system-aarch64
#   - qemu-efi-aarch64 firmware image
#   - Disk images built with: make ARCH=aarch64 TEST=1 bin/aarch64/sdcard.img
#
# Exit codes:
#   0  All tests passed
#   1  One or more tests failed, timed out, or did not complete
# ==========================================================================

set -euo pipefail

BINDIR="${BINDIR:-bin/aarch64}"
AAVMF="${AAVMF:-/usr/share/qemu-efi-aarch64/QEMU_EFI.fd}"
TIMEOUT="${TIMEOUT:-180}"
SERIAL_LOG="$(mktemp /tmp/zonix-aarch64-ci-XXXXXX.log)"
QEMU="qemu-system-aarch64"
QEMU_PID=""

cleanup() {
    if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
    rm -f "$SERIAL_LOG"
}
trap cleanup EXIT

echo "=== Zonix AArch64 CI Test Runner ==="
echo "  Timeout: ${TIMEOUT}s"
echo "  Log:     $SERIAL_LOG"
echo ""

if [ ! -f "$AAVMF" ]; then
    echo "ERROR: AArch64 UEFI firmware not found at $AAVMF"
    echo "       Install qemu-efi-aarch64 or set AAVMF= env var"
    exit 1
fi

if [ ! -f "${BINDIR}/zonix-uefi.img" ]; then
    echo "ERROR: ${BINDIR}/zonix-uefi.img not found."
    echo "       Run: make ARCH=aarch64 TEST=1 bin/aarch64/sdcard.img"
    exit 1
fi

if [ ! -f "${BINDIR}/sdcard.img" ]; then
    echo "ERROR: ${BINDIR}/sdcard.img not found."
    echo "       Run: make ARCH=aarch64 TEST=1 bin/aarch64/sdcard.img"
    exit 1
fi

QEMU_CMD=(
    "$QEMU"
    -M virt
    -cpu cortex-a72
    -m 256M
    -bios "$AAVMF"
    -display none
    -no-reboot
    -serial "file:${SERIAL_LOG}"
    -drive "file=${BINDIR}/zonix-uefi.img,format=raw,if=none,id=sys"
    -device "virtio-blk-pci,drive=sys"
    -drive "file=${BINDIR}/sdcard.img,format=raw,if=none,id=sdcard"
    -device sdhci-pci
    -device "sd-card,drive=sdcard"
)

echo "Starting QEMU (aarch64 UEFI mode)..."
echo "  ${QEMU_CMD[*]}"
echo ""

"${QEMU_CMD[@]}" &
QEMU_PID=$!

COMPLETE=0
TIMED_OUT=1
for _ in $(seq 1 "$TIMEOUT"); do
    if grep -q 'CI_TEST_COMPLETE' "$SERIAL_LOG" 2>/dev/null; then
        COMPLETE=1
        TIMED_OUT=0
        break
    fi

    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        TIMED_OUT=0
        break
    fi

    sleep 1
done

echo ""
if [ "$COMPLETE" -eq 1 ]; then
    echo "CI_TEST_COMPLETE marker found, stopping QEMU..."
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
elif [ "$TIMED_OUT" -eq 1 ]; then
    echo "QEMU did not finish within ${TIMEOUT}s, stopping..."
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
fi

echo ""
echo "=== Serial Output (last 120 lines) ==="
tail -n 120 "$SERIAL_LOG" 2>/dev/null || echo "(empty)"
echo "=== End Serial Output ==="
echo ""

FAIL_COUNT=0
PASS_COUNT=0
if [ -f "$SERIAL_LOG" ]; then
    FAIL_COUNT=$(grep -c '\[FAIL\]' "$SERIAL_LOG" 2>/dev/null || true)
    PASS_COUNT=$(grep -c '\[OK\]' "$SERIAL_LOG" 2>/dev/null || true)
fi

echo "=== Results ==="
echo "  Assertions passed: $PASS_COUNT"
echo "  Assertions failed: $FAIL_COUNT"
echo "  Test suite completed: $([ "$COMPLETE" -eq 1 ] && echo 'yes' || echo 'NO')"

if [ "$TIMED_OUT" -eq 1 ]; then
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
