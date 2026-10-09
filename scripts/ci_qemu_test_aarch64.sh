#!/usr/bin/env bash
# ==========================================================================
# ci_qemu_test_aarch64.sh — Boot Zonix aarch64 in qemu and verify test logs
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
serial_log="$(mktemp /tmp/zonix-aarch64-ci-XXXXXX.log)"
qemu="qemu-system-aarch64"
qemu_pid=""

cleanup() {
    if [ -n "$qemu_pid" ] && kill -0 "$qemu_pid" 2>/dev/null; then
        kill "$qemu_pid" 2>/dev/null || true
        wait "$qemu_pid" 2>/dev/null || true
    fi
    rm -f "$serial_log"
}
trap cleanup EXIT

echo "=== Zonix AArch64 CI Test Runner ==="
echo "  Timeout: ${TIMEOUT}s"
echo "  Log:     $serial_log"
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
    "$qemu"
    -M virt
    -cpu cortex-a72
    -m 256M
    -bios "$AAVMF"
    -display none
    -no-reboot
    -serial "file:${serial_log}"
    -drive "file=${BINDIR}/zonix-uefi.img,format=raw,if=none,id=sys"
    -device "virtio-blk-pci,drive=sys"
    -drive "file=${BINDIR}/sdcard.img,format=raw,if=none,id=sdcard"
    -device sdhci-pci
    -device "sd-card,drive=sdcard"
)

echo "Starting qemu (aarch64 UEFI mode)..."
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
