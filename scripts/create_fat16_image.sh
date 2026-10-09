#!/bin/bash
# Create FAT16 test image for Zonix OS

set -e

image_path="bin/fat16_test.img"
mount_point="/tmp/zonix_fat16_mount"
image_size_mib=16

echo "=== Creating FAT16 Test Image ==="

# Create bin directory if not exists
mkdir -p bin

# Create 4MB disk image
echo "Creating ${image_size_mib}MB disk image..."
dd if=/dev/zero of=$image_path bs=1M count=$image_size_mib status=progress

# Format as FAT16
echo "Formatting as FAT16..."
mkfs.vfat -F 16 -n "ZONIX" $image_path

# Create mount point
echo "Creating mount point..."
sudo mkdir -p $mount_point

# Mount the image
echo "Mounting image..."
sudo mount -o loop $image_path $mount_point

# Create test files
echo "Creating test files..."

# 1. Hello world file
echo "Hello from Zonix FAT16!" | sudo tee $mount_point/HELLO.TXT > /dev/null

# 2. README file
cat << 'EOF' | sudo tee $mount_point/README.TXT > /dev/null
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
cat << 'EOF' | sudo tee $mount_point/TEST.TXT > /dev/null
This is line 1
This is line 2
This is line 3
This is line 4
This is line 5
EOF

# 4. Numbers file
seq 1 100 | sudo tee $mount_point/NUMBERS.TXT > /dev/null

# 5. Lorem ipsum
cat << 'EOF' | sudo tee $mount_point/LOREM.TXT > /dev/null
Lorem ipsum dolor sit amet, consectetur adipiscing elit.
Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.
Duis aute irure dolor in reprehenderit in voluptate velit esse.
Excepteur sint occaecat cupidatat non proident, sunt in culpa.
EOF

# 6. System info (short 8.3 name)
cat << 'EOF' | sudo tee $mount_point/SYSINFO.TXT > /dev/null
Zonix OS Version 0.4.0
FAT16 File System Test
Build: 2025-11-12
EOF

# Create a subdirectory (note: we don't support subdirectories yet)
# sudo mkdir -p $mount_point/TESTDIR
# echo "Subdirectory test" | sudo tee $mount_point/TESTDIR/SUBFILE.TXT > /dev/null

# List files
echo ""
echo "Files created:"
ls -lh $mount_point/

# Unmount
echo ""
echo "Unmounting..."
sudo umount $mount_point

# Clean up mount point
sudo rmdir $mount_point

echo ""
echo "=== FAT16 Test Image Created Successfully ==="
echo "Image: $image_path"
echo "Size: ${image_size_mib}MB"
echo ""
echo "To use in Zonix:"
echo "1. Make sure bochsrc.bxrc includes this disk as ata0-slave:"
echo "   ata0-slave: type=disk, path=\"$image_path\", mode=flat"
echo ""
echo "2. In Zonix shell, run:"
echo "   fatmount"
echo "   fatls"
echo "   fatcat HELLO.TXT"
echo ""
