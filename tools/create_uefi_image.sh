#!/bin/bash
# Create UEFI bootable disk image for Zonix

set -e

# Configuration
IMAGE="bin/zonix-uefi.img"
IMAGE_SIZE_MB=100
BOOTLOADER="bin/BOOTX64.EFI"
KERNEL="bin/kernel"

# Check required files exist
if [ ! -f "$BOOTLOADER" ]; then
    echo "Error: UEFI bootloader not found: $BOOTLOADER"
    exit 1
fi

if [ ! -f "$KERNEL" ]; then
    echo "Error: Kernel not found: $KERNEL"
    exit 1
fi

echo "Creating UEFI disk image: $IMAGE"

# Create empty image
dd if=/dev/zero of="$IMAGE" bs=1M count=$IMAGE_SIZE_MB status=progress

# Create GPT partition table with EFI System Partition (ESP)
echo "Creating GPT partition table..."
parted -s "$IMAGE" mklabel gpt
parted -s "$IMAGE" mkpart ESP fat32 1MiB 100%
parted -s "$IMAGE" set 1 boot on

# Format ESP as FAT32
echo "Formatting ESP partition..."
LOOP_DEV=$(sudo losetup -fP --show "$IMAGE")
sudo mkfs.fat -F 32 -n "ZONIX_EFI" "${LOOP_DEV}p1"

# Mount ESP and copy files
echo "Installing bootloader and kernel..."
MOUNT_POINT="/tmp/zonix_esp_$$"
mkdir -p "$MOUNT_POINT"
sudo mount "${LOOP_DEV}p1" "$MOUNT_POINT"

# Create EFI directory structure
sudo mkdir -p "$MOUNT_POINT/EFI/BOOT"
sudo mkdir -p "$MOUNT_POINT/EFI/ZONIX"

# Copy UEFI bootloader (must be named BOOTX64.EFI for x64 systems)
sudo cp "$BOOTLOADER" "$MOUNT_POINT/EFI/BOOT/BOOTX64.EFI"

# Copy kernel
sudo cp "$KERNEL" "$MOUNT_POINT/EFI/ZONIX/KERNEL.ELF"
sudo cp "$KERNEL" "$MOUNT_POINT/KERNEL.ELF"  # Alternative path

# Unmount and cleanup
sudo umount "$MOUNT_POINT"
sudo losetup -d "$LOOP_DEV"
rmdir "$MOUNT_POINT"

echo "UEFI disk image created successfully: $IMAGE"
echo "Boot with OVMF/UEFI firmware using:"
echo "  qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -drive file=$IMAGE,format=raw"
