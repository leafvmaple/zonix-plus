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
        *riscv64*) ARCH=riscv64 ;;
        *)         ARCH=x86 ;;
    esac
fi

image_path="${BINDIR}/zonix-uefi.img"

case "$ARCH" in
    aarch64)
        bootloader_path="${BINDIR}/BOOTAA64.EFI"
        efi_boot_name="BOOTAA64.EFI"
        image_size_mib=128
        ;;
    riscv64)
        bootloader_path="${BINDIR}/BOOTRISCV64.EFI"
        efi_boot_name="BOOTRISCV64.EFI"
        image_size_mib=128
        ;;
    *)
        bootloader_path="${BINDIR}/BOOTX64.EFI"
        efi_boot_name="BOOTX64.EFI"
        image_size_mib=100
        ;;
esac

kernel_path="${BINDIR}/kernel"

[ -f "$bootloader_path" ] || { echo "Error: $bootloader_path not found"; exit 1; }
[ -f "$kernel_path" ] || { echo "Error: $kernel_path not found"; exit 1; }

echo "[1] Creating ${image_size_mib}MB image..."
dd if=/dev/zero of="$image_path" bs=1M count=$image_size_mib 2>/dev/null

echo "[2] Creating GPT partition table..."
parted -s "$image_path" mklabel gpt
parted -s "$image_path" mkpart "ESP" fat32 1MiB 100%
parted -s "$image_path" set 1 esp on
parted -s "$image_path" set 1 boot on

echo "[3] Formatting ESP partition as FAT32..."
mkfs.fat -F 32 -n "ESP" --offset 2048 "$image_path" 2>/dev/null

echo "[4] Getting absolute path..."
image_absolute_path="$(cd "$(dirname "$image_path")" && pwd)/$(basename "$image_path")"

echo "[5] Creating EFI directory structure (using mtools)..."
mtools_image="${image_absolute_path}@@1M"

mmd -i "$mtools_image" ::/EFI 2>/dev/null || true
mmd -i "$mtools_image" ::/EFI/BOOT 2>/dev/null || true
mmd -i "$mtools_image" ::/EFI/ZONIX 2>/dev/null || true

echo "[6] Copying files..."
mcopy -i "$mtools_image" "$bootloader_path" "::/EFI/BOOT/${efi_boot_name}"
mcopy -i "$mtools_image" "$kernel_path" ::/EFI/ZONIX/KERNEL.ELF
mcopy -i "$mtools_image" "$kernel_path" ::/KERNEL.ELF

echo "[7] Creating startup.nsh..."
startup_script=$(mktemp)
cat > "$startup_script" << NSH_EOF
fs0:
cd \\EFI\\BOOT
${efi_boot_name}
NSH_EOF
mcopy -i "$mtools_image" "$startup_script" ::/startup.nsh
rm -f "$startup_script"

echo "[8] Verifying (listing files)..."
mdir -i "$mtools_image" ::/EFI/BOOT
mdir -i "$mtools_image" ::/EFI/ZONIX

echo "[9] Done! GPT+ESP image created."
ls -lh "$image_path"
fdisk -l "$image_path" 2>/dev/null | head -15 || true
