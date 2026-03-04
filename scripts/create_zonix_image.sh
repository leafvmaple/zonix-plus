#!/bin/bash
# Create FAT32 disk image for Zonix OS (BIOS boot)
# Called from: make bin/zonix.img
set -e

IMAGE="bin/zonix.img"

echo "Creating FAT32 disk image..."

# Create 64MB disk image (FAT32 requires at least 33MB)
dd if=/dev/zero of="$IMAGE" bs=1M count=64 2>/dev/null

# Write MBR
dd if=bin/mbr.bin of="$IMAGE" bs=446 count=1 conv=notrunc 2>/dev/null

# Create partition table (partition starts at sector 1)
echo -e "label: dos\nstart=1, size=131071, type=0c, bootable" | sfdisk "$IMAGE" 2>/dev/null

# Format partition as FAT32
mkfs.fat -F 32 -n "ZONIX" -S 512 -s 8 -R 32 -f 2 --offset 1 "$IMAGE" 2>/dev/null

# Copy kernel using mtools (no sudo required)
# Configure mtools to access the partition at offset 512 (sector 1)
echo "drive z: file=\"$IMAGE\" offset=512" > /tmp/mtoolsrc_zonix
MTOOLSRC=/tmp/mtoolsrc_zonix mcopy -i "$IMAGE"@@512 bin/kernel ::KERNEL.SYS
rm -f /tmp/mtoolsrc_zonix

# Install custom VBR boot code (preserve BPB)
# For FAT32, BPB is 90 bytes (0x00-0x59)
dd if="$IMAGE" of=temp_bpb.bin bs=1 skip=512 count=90 2>/dev/null
dd if=bin/vbr.bin of=temp_bootcode.bin bs=1 skip=90 count=420 2>/dev/null
dd if=bin/vbr.bin of=temp_signature.bin bs=1 skip=510 count=2 2>/dev/null
cat temp_bpb.bin temp_bootcode.bin temp_signature.bin > temp_vbr.bin
dd if=temp_vbr.bin of="$IMAGE" bs=1 seek=512 count=512 conv=notrunc 2>/dev/null
rm -f temp_bpb.bin temp_bootcode.bin temp_signature.bin temp_vbr.bin

# Install bootloader (after reserved sectors)
# Place at sector 2 for safety (offset = 1024)
dd if=bin/bootloader.bin of="$IMAGE" bs=1 seek=1024 conv=notrunc 2>/dev/null

echo "FAT32 image created: $IMAGE"
