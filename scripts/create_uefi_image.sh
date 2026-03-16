#!/bin/bash
# Create a GPT+ESP UEFI boot image for Zonix OS.
# Works for both x86_64 and AArch64 — auto-detects from ARCH or boot binary.
#
# Environment:
#   BINDIR   — directory containing the boot binary and kernel (required)
#   ARCH     — x86 or aarch64 (auto-detected from BINDIR if omitted)
set -e

BINDIR="${BINDIR:-bin}"

# Auto-detect architecture from BINDIR path
if [ -z "$ARCH" ]; then
    case "$BINDIR" in
        *aarch64*) ARCH=aarch64 ;;
        *)         ARCH=x86 ;;
    esac
fi

IMAGE="${BINDIR}/zonix-uefi.img"

case "$ARCH" in
    aarch64)
        BOOTLOADER="${BINDIR}/BOOTAA64.EFI"
        EFI_BOOT_NAME="BOOTAA64.EFI"
        IMAGE_SIZE=128
        ;;
    *)
        BOOTLOADER="${BINDIR}/BOOTX64.EFI"
        EFI_BOOT_NAME="BOOTX64.EFI"
        IMAGE_SIZE=100
        ;;
esac

KERNEL="${BINDIR}/kernel"

[ -f "$BOOTLOADER" ] || { echo "Error: $BOOTLOADER not found"; exit 1; }
[ -f "$KERNEL" ] || { echo "Error: $KERNEL not found"; exit 1; }

echo "[1] Creating ${IMAGE_SIZE}MB image..."
dd if=/dev/zero of="$IMAGE" bs=1M count=$IMAGE_SIZE 2>/dev/null

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
mcopy -i "$MTOOLS_IMG" "$BOOTLOADER" "::/EFI/BOOT/${EFI_BOOT_NAME}"
mcopy -i "$MTOOLS_IMG" "$KERNEL" ::/EFI/ZONIX/KERNEL.ELF
mcopy -i "$MTOOLS_IMG" "$KERNEL" ::/KERNEL.ELF

echo "[7] Creating startup.nsh..."
TMPNSH=$(mktemp)
cat > "$TMPNSH" << NSH_EOF
fs0:
cd \\EFI\\BOOT
${EFI_BOOT_NAME}
NSH_EOF
mcopy -i "$MTOOLS_IMG" "$TMPNSH" ::/startup.nsh
rm -f "$TMPNSH"

echo "[8] Verifying (listing files)..."
mdir -i "$MTOOLS_IMG" ::/EFI/BOOT
mdir -i "$MTOOLS_IMG" ::/EFI/ZONIX

echo "[9] Done! GPT+ESP image created."
ls -lh "$IMAGE"
fdisk -l "$IMAGE" 2>/dev/null | head -15 || true
