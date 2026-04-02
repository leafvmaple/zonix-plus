#!/usr/bin/env bash
# ==========================================================================
# ci_qemu_test_riscv64.sh — Boot Zonix riscv64 in QEMU and verify test logs
#
# Usage:
#   ./scripts/ci_qemu_test_riscv64.sh
#
# Requires:
#   - qemu-system-riscv64
#   - qemu-efi-riscv64 firmware image
#   - Disk images built with: make ARCH=riscv64 TEST=1 bin/riscv64/sdcard.img
#
# Exit codes:
#   0  All tests passed
#   1  One or more tests failed, timed out, or did not complete
# ==========================================================================

set -euo pipefail

BINDIR="${BINDIR:-bin/riscv64}"
RISCV_FW="${RISCV_FW:-/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd}"
RISCV_VARS="${RISCV_VARS:-/usr/share/qemu-efi-riscv64/RISCV_VIRT_VARS.fd}"
TIMEOUT="${TIMEOUT:-180}"
SERIAL_LOG="$(mktemp /tmp/zonix-riscv64-ci-XXXXXX.log)"
QEMU="qemu-system-riscv64"
QEMU_PID=""

cleanup() {
    if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
    rm -f "$SERIAL_LOG"
}
trap cleanup EXIT

echo "=== Zonix RISC-V 64 CI Test Runner ==="
echo "  Timeout: ${TIMEOUT}s"
echo "  Log:     $SERIAL_LOG"
echo ""

if [ ! -f "$RISCV_FW" ]; then
    echo "ERROR: RISC-V UEFI firmware not found at $RISCV_FW"
    echo "       Install qemu-efi-riscv64 or set RISCV_FW= env var"
    exit 1
fi

if [ ! -f "$RISCV_VARS" ]; then
    echo "ERROR: RISC-V UEFI vars template not found at $RISCV_VARS"
    echo "       Install qemu-efi-riscv64 or set RISCV_VARS= env var"
    exit 1
fi

if [ ! -f "${BINDIR}/zonix-uefi.img" ]; then
    echo "ERROR: ${BINDIR}/zonix-uefi.img not found."
    echo "       Run: make ARCH=riscv64 TEST=1 bin/riscv64/sdcard.img"
    exit 1
fi

if [ ! -f "${BINDIR}/sdcard.img" ]; then
    echo "ERROR: ${BINDIR}/sdcard.img not found."
    echo "       Run: make ARCH=riscv64 TEST=1 bin/riscv64/sdcard.img"
    exit 1
fi

# Create a writable copy of the UEFI variable store
VARS_COPY="$(mktemp /tmp/zonix-riscv64-vars-XXXXXX.fd)"
cp "$RISCV_VARS" "$VARS_COPY"

QEMU_CMD=(
    "$QEMU"
    -M virt
    -cpu rv64
    -m 256M
    -drive "if=pflash,format=raw,unit=0,file=${RISCV_FW},readonly=on"
    -drive "if=pflash,format=raw,unit=1,file=${VARS_COPY}"
    -display none
    -no-reboot
    -serial "file:${SERIAL_LOG}"
    -drive "file=${BINDIR}/zonix-uefi.img,format=raw,if=none,id=sys"
    -device "virtio-blk-pci,drive=sys"
    -drive "file=${BINDIR}/sdcard.img,format=raw,if=none,id=sdcard"
    -device sdhci-pci
    -device "sd-card,drive=sdcard"
)

echo "Starting QEMU (riscv64 UEFI mode)..."
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

# Clean up the temporary vars file
rm -f "$VARS_COPY"

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
