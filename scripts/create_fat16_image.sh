#!/bin/bash
# Create FAT16 test image for Zonix OS

set -e

IMAGE="bin/fat16_test.img"
MOUNT_POINT="/tmp/zonix_fat16_mount"
SIZE_MB=16

echo "=== Creating FAT16 Test Image ==="

# Create bin directory if not exists
mkdir -p bin

# Create 4MB disk image
echo "Creating ${SIZE_MB}MB disk image..."
dd if=/dev/zero of=$IMAGE bs=1M count=$SIZE_MB status=progress

# Format as FAT16
echo "Formatting as FAT16..."
mkfs.vfat -F 16 -n "ZONIX" $IMAGE

# Create mount point
echo "Creating mount point..."
sudo mkdir -p $MOUNT_POINT

# Mount the image
echo "Mounting image..."
sudo mount -o loop $IMAGE $MOUNT_POINT

# Create test files
echo "Creating test files..."

# 1. Hello world file
echo "Hello from Zonix FAT16!" | sudo tee $MOUNT_POINT/HELLO.TXT > /dev/null

# 2. README file
cat << 'EOF' | sudo tee $MOUNT_POINT/README.TXT > /dev/null
=================================
Zonix OS - FAT16 Test File System
=================================

This is a test FAT16 file system for Zonix OS.

Features:
- FAT16 file system support
- Read-only operations
- Directory listing (fatls)
- File reading (fatcat)

Test files included:
1. HELLO.TXT - Simple greeting
2. README.TXT - This file
3. TEST.TXT - Multi-line test
4. NUMBERS.TXT - Number sequence
5. LOREM.TXT - Lorem ipsum text

Commands to try:
  fatmount  - Mount the FAT16 file system
  fatls     - List files
  fatcat HELLO.TXT - Display file contents

Enjoy exploring!
EOF

# 3. Test file
cat << 'EOF' | sudo tee $MOUNT_POINT/TEST.TXT > /dev/null
This is line 1
This is line 2
This is line 3
This is line 4
This is line 5
EOF

# 4. Numbers file
seq 1 100 | sudo tee $MOUNT_POINT/NUMBERS.TXT > /dev/null

# 5. Lorem ipsum
cat << 'EOF' | sudo tee $MOUNT_POINT/LOREM.TXT > /dev/null
Lorem ipsum dolor sit amet, consectetur adipiscing elit.
Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.
Duis aute irure dolor in reprehenderit in voluptate velit esse.
Excepteur sint occaecat cupidatat non proident, sunt in culpa.
EOF

# 6. System info (short 8.3 name)
cat << 'EOF' | sudo tee $MOUNT_POINT/SYSINFO.TXT > /dev/null
Zonix OS Version 0.4.0
FAT16 File System Test
Build: 2025-11-12
EOF

# Create a subdirectory (note: we don't support subdirectories yet)
# sudo mkdir -p $MOUNT_POINT/TESTDIR
# echo "Subdirectory test" | sudo tee $MOUNT_POINT/TESTDIR/SUBFILE.TXT > /dev/null

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
echo "=== FAT16 Test Image Created Successfully ==="
echo "Image: $IMAGE"
echo "Size: ${SIZE_MB}MB"
echo ""
echo "To use in Zonix:"
echo "1. Make sure bochsrc.bxrc includes this disk as ata0-slave:"
echo "   ata0-slave: type=disk, path=\"$IMAGE\", mode=flat"
echo ""
echo "2. In Zonix shell, run:"
echo "   fatmount"
echo "   fatls"
echo "   fatcat HELLO.TXT"
echo ""
