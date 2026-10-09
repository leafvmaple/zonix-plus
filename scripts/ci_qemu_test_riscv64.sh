#!/usr/bin/env bash
# ==========================================================================
# ci_qemu_test_riscv64.sh — Boot Zonix riscv64 in qemu and verify test logs
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
serial_log="$(mktemp /tmp/zonix-riscv64-ci-XXXXXX.log)"
qemu="qemu-system-riscv64"
qemu_pid=""

cleanup() {
    if [ -n "$qemu_pid" ] && kill -0 "$qemu_pid" 2>/dev/null; then
        kill "$qemu_pid" 2>/dev/null || true
        wait "$qemu_pid" 2>/dev/null || true
    fi
    rm -f "$serial_log"
}
trap cleanup EXIT

echo "=== Zonix RISC-V 64 CI Test Runner ==="
echo "  Timeout: ${TIMEOUT}s"
echo "  Log:     $serial_log"
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
vars_copy="$(mktemp /tmp/zonix-riscv64-vars-XXXXXX.fd)"
cp "$RISCV_VARS" "$vars_copy"

QEMU_CMD=(
    "$qemu"
    -M virt
    -cpu rv64
    -m 256M
    -drive "if=pflash,format=raw,unit=0,file=${RISCV_FW},readonly=on"
    -drive "if=pflash,format=raw,unit=1,file=${vars_copy}"
    -display none
    -no-reboot
    -serial "file:${serial_log}"
    -drive "file=${BINDIR}/zonix-uefi.img,format=raw,if=none,id=sys"
    -device "virtio-blk-pci,drive=sys"
    -drive "file=${BINDIR}/sdcard.img,format=raw,if=none,id=sdcard"
    -device sdhci-pci
    -device "sd-card,drive=sdcard"
)

echo "Starting qemu (riscv64 UEFI mode)..."
echo "  ${QEMU_CMD[*]}"
echo ""

"${QEMU_CMD[@]}" &
qemu_pid=$!

complete=0
timed_out=1
for _ in $(seq 1 "$TIMEOUT"); do
    if grep -q 'CI_TEST_COMPLETE' "$serial_log" 2>/dev/null; then
        complete=1
        timed_out=0
        break
    fi

    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        timed_out=0
        break
    fi

    sleep 1
done

echo ""
if [ "$complete" -eq 1 ]; then
    echo "CI_TEST_COMPLETE marker found, stopping qemu..."
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
elif [ "$timed_out" -eq 1 ]; then
    echo "qemu did not finish within ${TIMEOUT}s, stopping..."
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
fi

# Clean up the temporary vars file
rm -f "$vars_copy"

echo ""
echo "=== Serial Output (last 120 lines) ==="
tail -n 120 "$serial_log" 2>/dev/null || echo "(empty)"
echo "=== End Serial Output ==="
echo ""

fail_count=0
pass_count=0
if [ -f "$serial_log" ]; then
    fail_count=$(grep -c '\[FAIL\]' "$serial_log" 2>/dev/null || true)
    pass_count=$(grep -c '\[OK\]' "$serial_log" 2>/dev/null || true)
fi

echo "=== Results ==="
echo "  Assertions passed: $pass_count"
echo "  Assertions failed: $fail_count"
echo "  Test suite completed: $([ "$complete" -eq 1 ] && echo 'yes' || echo 'NO')"

if [ "$timed_out" -eq 1 ]; then
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
