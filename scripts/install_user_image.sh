#!/usr/bin/env bash
# Explicit user-disk installation. System image recipes never call this script.
set -euo pipefail
image="$1"
shift
if [ ! -f "$image" ]; then
    mkdir -p "$(dirname "$image")"
    dd if=/dev/zero of="$image" bs=1048576 count=64 2>/dev/null
    mkfs.vfat -F 32 -n ZONIXUSER "$image" >/dev/null
fi
for program in "$@"; do
    mcopy -o -i "$image" "$program" "::/$(basename "$program")"
done
