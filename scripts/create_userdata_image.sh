#!/bin/bash
# =========================================================================
# Create FAT32 data disk for Zonix OS (second disk / hdb)
#
# Contains:
#   - User-mode ELF programs (from bin/user/*.ELF)
#   - Test data files
#
# Uses mtools (mcopy) so NO sudo / loop-mount is needed.
# =========================================================================

set -e

BINDIR="${BINDIR:-bin}"
IMAGE="${BINDIR}/userdata.img"
USER_BINDIR="${BINDIR}/user"
SIZE_MB=64

echo "=== Creating FAT32 Data Disk ==="

mkdir -p "$BINDIR"

# 1. Create raw image
echo "Creating ${SIZE_MB}MB disk image..."
dd if=/dev/zero of="$IMAGE" bs=1M count=$SIZE_MB status=none

# 2. Format as FAT32
echo "Formatting as FAT32..."
mkfs.vfat -F 32 -n "ZONIXDATA" "$IMAGE" > /dev/null

# ---- helper: write a string to a file on the image ----
put_text() {
    local dst="$1"   # e.g. "::/$NAME"
    local tmp
    tmp=$(mktemp)
    cat > "$tmp"
    mcopy -i "$IMAGE" "$tmp" "$dst"
    rm -f "$tmp"
}

# 3. Copy user-mode ELF programs
echo "Copying user programs..."
elf_count=0
if [ -d "$USER_BINDIR" ]; then
    for elf in "$USER_BINDIR"/*.ELF; do
        [ -f "$elf" ] || continue
        name=$(basename "$elf")
        mcopy -i "$IMAGE" "$elf" "::/$name"
        echo "  + $name ($(stat -c%s "$elf") bytes)"
        elf_count=$((elf_count + 1))
    done
fi
echo "  $elf_count program(s) copied."

# 4. Create test data files
echo "Creating test data files..."

echo "Hello from Zonix FAT32!" | put_text "::/HELLO.TXT"

cat << 'EOF' | put_text "::/README.TXT"
=================================
Zonix OS - Data Disk
=================================

This disk contains user-mode programs and test data.

Usage in Zonix shell:
  mount hdb       Mount this disk at /mnt
  ls /mnt         List files
  exec HELLO.ELF /mnt   Run a user program
  cat HELLO.TXT /mnt    Display a text file
  umount          Unmount

Enjoy!
EOF

cat << 'EOF' | put_text "::/TEST.TXT"
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

seq 1 200 | put_text "::/NUMBERS.TXT"

cat << 'EOF' | put_text "::/LOREM.TXT"
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

# Larger file for testing cluster chains
tmpfile=$(mktemp)
for i in $(seq 1 100); do
    echo "Line $i: The quick brown fox jumps over the lazy dog. 0123456789"
done > "$tmpfile"
mcopy -i "$IMAGE" "$tmpfile" "::/LARGE.TXT"
rm -f "$tmpfile"

cat << 'EOF' | put_text "::/SYSINFO.TXT"
Zonix OS Data Disk
Architecture: x86_64
EOF

# 5. List contents
echo ""
echo "Disk contents:"
mdir -i "$IMAGE" ::/

echo ""
echo "=== FAT32 Data Disk Created ==="
echo "Image: $IMAGE  (${SIZE_MB}MB)"
echo ""
echo "In Zonix shell:"
echo "  mount hdb"
echo "  ls /mnt"
echo "  exec HELLO.ELF /mnt"
echo ""
