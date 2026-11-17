#!/usr/bin/env bash
# Base rootfs and extra applications are installed independently of boot images.
set -euo pipefail
kind="$1"
image="$2"
shift 2
case "$kind" in
    base) label=ZONIXBASE; directories="bin sbin dev etc home opt tmp mnt" ;;
    apps) label=ZONIXAPPS; directories="bin" ;;
    *) echo "Unknown userspace image kind: $kind" >&2; exit 1 ;;
esac
if [ ! -f "$image" ]; then
    mkdir -p "$(dirname "$image")"
    dd if=/dev/zero of="$image" bs=1048576 count=64 2>/dev/null
    mkfs.vfat -F 32 -n "$label" "$image" >/dev/null
fi
for directory in $directories; do
    if ! mdir -i "$image" "::/$directory" >/dev/null 2>&1; then
        mmd -i "$image" "::/$directory"
    fi
done
if [ "$kind" = base ]; then
    mcopy -o -i "$image" "$1" ::/sbin/init
    mcopy -o -i "$image" "$2" ::/bin/sh
else
    for program in "$@"; do
        name="$(basename "$program" .ELF)"
        mcopy -o -i "$image" "$program" "::/bin/$name"
    done
fi
