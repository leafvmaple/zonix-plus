#!/usr/bin/env bash
# User ELF preparation belongs to the test runner, never system-image recipes.
set -euo pipefail
arch="$1"
directory="$2"
make ARCH="$arch" TEST=1 BINDIR="$directory" user "$directory/userdata.img"
for program in "$directory"/user/*.ELF; do
    mcopy -o -i "$directory/userdata.img" "$program" ::/
done
