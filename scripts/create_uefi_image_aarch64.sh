#!/bin/bash
set -e

BINDIR="${BINDIR:-bin/aarch64}"
IMAGE="${BINDIR}/zonix-uefi.img"
BOOTLOADER="${BINDIR}/BOOTAA64.EFI"
KERNEL="${BINDIR}/kernel"

[ -f "$BOOTLOADER" ] || { echo "Error: $BOOTLOADER not found"; exit 1; }
[ -f "$KERNEL" ] || { echo "Error: $KERNEL not found"; exit 1; }

echo "[1] Creating 100MB image..."
dd if=/dev/zero of="$IMAGE" bs=1M count=100 2>/dev/null

echo "[2] Creating GPT partition table..."
parted -s "$IMAGE" mklabel gpt
parted -s "$IMAGE" mkpart "ESP" fat32 1MiB 100%
parted -s "$IMAGE" set 1 esp on
parted -s "$IMAGE" set 1 boot on

echo "[3] Formatting ESP partition as FAT32..."
mkfs.fat -F 32 -n "ESP" --offset 2048 "$IMAGE" 2>/dev/null

echo "[4] Getting absolute path..."
IMAGE_ABS="$(cd "$(dirname "$IMAGE")" && pwd)/$(basename "$IMAGE")"

echo "[5] Creating EFI directory structure (using mtools)..."
MTOOLS_IMG="${IMAGE_ABS}@@1M"

mmd -i "$MTOOLS_IMG" ::/EFI 2>/dev/null || true
mmd -i "$MTOOLS_IMG" ::/EFI/BOOT 2>/dev/null || true
mmd -i "$MTOOLS_IMG" ::/EFI/ZONIX 2>/dev/null || true

echo "[6] Copying files..."
mcopy -i "$MTOOLS_IMG" "$BOOTLOADER" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$MTOOLS_IMG" "$KERNEL" ::/EFI/ZONIX/KERNEL.ELF
mcopy -i "$MTOOLS_IMG" "$KERNEL" ::/KERNEL.ELF

echo "[7] Creating startup.nsh..."
cat > /tmp/startup_aa64.nsh << 'NSH_EOF'
fs0:
cd \EFI\BOOT
BOOTAA64.EFI
NSH_EOF
mcopy -i "$MTOOLS_IMG" /tmp/startup_aa64.nsh ::/startup.nsh
rm -f /tmp/startup_aa64.nsh

echo "[8] Verifying (listing files)..."
mdir -i "$MTOOLS_IMG" ::/EFI/BOOT
mdir -i "$MTOOLS_IMG" ::/EFI/ZONIX

echo "[9] Done! GPT+ESP image created."
ls -lh "$IMAGE"
fdisk -l "$IMAGE" 2>/dev/null | head -15 || true
