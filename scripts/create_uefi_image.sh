#!/bin/bash
# Create a GPT+ESP UEFI boot image for Zonix OS.
# Works for x86_64, AArch64 and RISC-V 64-bit — selects ARCH or infers it from BINDIR.
#
# Environment:
#   BINDIR   — directory containing the boot binary and kernel (default: bin)
#   ARCH     — x86, aarch64 or riscv64 (auto-detected from BINDIR if omitted)
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

echo "[1] Creating ${image_size_mib} MiB image..."
dd if=/dev/zero of="$image_path" bs=1048576 count=$image_size_mib 2>/dev/null

echo "[2] Creating GPT partition table..."
sgdisk --clear --new=1:2048:0 --typecode=1:ef00 --change-name=1:ESP "$image_path"

echo "[3] Formatting ESP partition as FAT32..."
# Leave the backup GPT (33 sectors at the end) outside the filesystem.
esp_blocks=$(( (image_size_mib * 2048 - 2048 - 33) / 2 ))
mkfs.fat -F 32 -n "ESP" -h 2048 --offset 2048 "$image_path" "$esp_blocks" 2>/dev/null

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
sgdisk --print "$image_path"
