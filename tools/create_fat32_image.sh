#!/bin/bash
# Create FAT32 test image for Zonix OS

set -e

IMAGE="bin/fat32_test.img"
MOUNT_POINT="/tmp/zonix_fat32_mount"
SIZE_MB=64

echo "=== Creating FAT32 Test Image ==="

# Create bin directory if not exists
mkdir -p bin

# Create disk image (FAT32 requires at least 33MB)
echo "Creating ${SIZE_MB}MB disk image..."
dd if=/dev/zero of=$IMAGE bs=1M count=$SIZE_MB status=progress

# Format as FAT32
echo "Formatting as FAT32..."
mkfs.vfat -F 32 -n "ZONIX" $IMAGE

# Create mount point
echo "Creating mount point..."
sudo mkdir -p $MOUNT_POINT

# Mount the image
echo "Mounting image..."
sudo mount -o loop $IMAGE $MOUNT_POINT

# Create test files
echo "Creating test files..."

# 1. Hello world file
echo "Hello from Zonix FAT32!" | sudo tee $MOUNT_POINT/HELLO.TXT > /dev/null

# 2. README file
cat << 'EOF' | sudo tee $MOUNT_POINT/README.TXT > /dev/null
=================================
Zonix OS - FAT32 Test File System
=================================

This is a test FAT32 file system for Zonix OS.

Features:
- FAT32 file system support
- Read-only operations
- Directory listing (ls)
- File reading (cat)
- Supports large volumes (>2GB)

Test files included:
1. HELLO.TXT - Simple greeting
2. README.TXT - This file
3. TEST.TXT - Multi-line test
4. NUMBERS.TXT - Number sequence
5. LOREM.TXT - Lorem ipsum text
6. LARGE.TXT - Larger test file

Commands to try:
  mount hd1  - Mount the FAT32 file system
  ls /mnt    - List files
  cat HELLO.TXT /mnt - Display file contents
  info       - Show filesystem information

Enjoy exploring FAT32!
EOF

# 3. Test file
cat << 'EOF' | sudo tee $MOUNT_POINT/TEST.TXT > /dev/null
This is line 1
This is line 2
This is line 3
This is line 4
This is line 5
This is line 6
This is line 7
This is line 8
This is line 9
This is line 10
EOF

# 4. Numbers file
seq 1 200 | sudo tee $MOUNT_POINT/NUMBERS.TXT > /dev/null

# 5. Lorem ipsum
cat << 'EOF' | sudo tee $MOUNT_POINT/LOREM.TXT > /dev/null
Lorem ipsum dolor sit amet, consectetur adipiscing elit.
Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.
Duis aute irure dolor in reprehenderit in voluptate velit esse.
Excepteur sint occaecat cupidatat non proident, sunt in culpa.

Sed ut perspiciatis unde omnis iste natus error sit voluptatem.
Accusantium doloremque laudantium, totam rem aperiam, eaque ipsa.
Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit.
At vero eos et accusamus et iusto odio dignissimos ducimus qui.
Blanditiis praesentium voluptatum deleniti atque corrupti quos.
EOF

# 6. Larger file for testing cluster chains
echo "Creating larger test file..."
for i in {1..100}; do
    echo "Line $i: The quick brown fox jumps over the lazy dog. 0123456789"
done | sudo tee $MOUNT_POINT/LARGE.TXT > /dev/null

# 7. System info
cat << 'EOF' | sudo tee $MOUNT_POINT/SYSINFO.TXT > /dev/null
Zonix OS Version 0.5.0
FAT32 File System Support
Build: 2025-12-02
Architecture: x86
EOF

# List files
echo ""
echo "Files created:"
ls -lh $MOUNT_POINT/

# Unmount
echo ""
echo "Unmounting..."
sudo umount $MOUNT_POINT

# Clean up mount point
sudo rmdir $MOUNT_POINT

echo ""
echo "=== FAT32 Test Image Created Successfully ==="
echo "Image: $IMAGE"
echo "Size: ${SIZE_MB}MB"
echo ""
echo "To use in Zonix:"
echo "1. Make sure bochsrc.bxrc includes this disk as ata0-slave:"
echo "   ata0-slave: type=disk, path=\"$IMAGE\", mode=flat"
echo ""
echo "2. In Zonix shell, run:"
echo "   mount hd1"
echo "   ls /mnt"
echo "   cat HELLO.TXT /mnt"
echo "   info"
echo ""
