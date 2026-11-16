#!/bin/bash
# Create FAT32 disk image for Zonix OS (BIOS boot)
# Called from: make ARCH=x86 bin/x86/zonix.img
set -e

BINDIR="${BINDIR:-bin}"
image_path="${BINDIR}/zonix.img"

echo "Creating FAT32 disk image..."

# Create a 64 MiB disk image for FAT32
dd if=/dev/zero of="$image_path" bs=1048576 count=64 2>/dev/null

# Write MBR
dd if=${BINDIR}/mbr.bin of="$image_path" bs=446 count=1 conv=notrunc 2>/dev/null

# Create partition table (partition starts at sector 1)
python3 - "$image_path" <<'PY_MBR'
import struct
import sys

# One active FAT32 LBA partition, sector 1 through the end of the image.
with open(sys.argv[1], "r+b") as image:
    image.seek(446)
    image.write(struct.pack("<B3sB3sII", 0x80, b"\x00\x02\x00", 0x0c,
                            b"\xfe\xff\xff", 1, 131071))
    image.write(bytes(48))
    image.write(b"\x55\xaa")
PY_MBR

# Format partition as FAT32
mkfs.fat -F 32 -n "ZONIX" -S 512 -s 8 -R 32 -b 0 -f 2 --offset 1 "$image_path" 2>/dev/null

# The second-stage loader occupies partition sectors 1 through 7. Keep
# writable FAT32 FSInfo in sector 31, outside that boot code; no backup VBR.
python3 - "$image_path" <<'PY_FSINFO'
import struct
import sys

with open(sys.argv[1], "r+b") as image:
    image.seek(2 * 512)
    fsinfo = image.read(512)
    image.seek(32 * 512)
    image.write(fsinfo)
    image.seek(512 + 48)
    image.write(struct.pack("<H", 31))
PY_FSINFO

# Copy kernel using mtools (no sudo required)
# Configure mtools to access the partition at offset 512 (sector 1)
echo "drive z: file=\"$image_path\" offset=512" > /tmp/mtoolsrc_zonix
MTOOLSRC=/tmp/mtoolsrc_zonix mcopy -i "$image_path"@@512 ${BINDIR}/kernel ::KERNEL.SYS
rm -f /tmp/mtoolsrc_zonix

# Install custom VBR boot code (preserve BPB)
# For FAT32, BPB is 90 bytes (0x00-0x59)
dd if="$image_path" of=temp_bpb.bin bs=1 skip=512 count=90 2>/dev/null
dd if=${BINDIR}/vbr.bin of=temp_bootcode.bin bs=1 skip=90 count=420 2>/dev/null
dd if=${BINDIR}/vbr.bin of=temp_signature.bin bs=1 skip=510 count=2 2>/dev/null
cat temp_bpb.bin temp_bootcode.bin temp_signature.bin > temp_vbr.bin
dd if=temp_vbr.bin of="$image_path" bs=1 seek=512 count=512 conv=notrunc 2>/dev/null
rm -f temp_bpb.bin temp_bootcode.bin temp_signature.bin temp_vbr.bin

# Install bootloader in the reserved area before the FAT
# Partition sector 1 (disk sector 2); FSInfo is stored separately at sector 31.
dd if=${BINDIR}/bootloader.bin of="$image_path" bs=1 seek=1024 conv=notrunc 2>/dev/null

echo "FAT32 image created: $image_path"
