#!/usr/bin/env bash
# Locate distro firmware first, then the firmware bundled alongside QEMU.
set -eu

if [ "${1:-}" = x86-bios ]; then
    # QEMU bundles split OVMF images; -bios needs the complete flash layout.
    code=${2:?Missing firmware source}
    output=${3:?Missing firmware destination}
    case "${code##*/}" in
        edk2-x86_64-code.fd)
            cat "${code%/*}/edk2-i386-vars.fd" "$code" > "$output"
            ;;
        *) cp "$code" "$output" ;;
    esac
    exit 0
fi

case "${1:?Usage: qemu_firmware.sh x86|aarch64|riscv64|riscv64-vars}" in
    x86)
        distro_path=/usr/share/ovmf/OVMF.fd
        distro_paths=("$distro_path" /usr/share/OVMF/OVMF.fd)
        bundled_name=edk2-x86_64-code.fd
        qemu=qemu-system-x86_64
        ;;
    aarch64)
        distro_path=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
        distro_paths=("$distro_path")
        bundled_name=edk2-aarch64-code.fd
        qemu=qemu-system-aarch64
        ;;
    riscv64)
        distro_path=/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd
        distro_paths=("$distro_path")
        bundled_name=edk2-riscv-code.fd
        qemu=qemu-system-riscv64
        ;;
    riscv64-vars)
        distro_path=/usr/share/qemu-efi-riscv64/RISCV_VIRT_VARS.fd
        distro_paths=("$distro_path")
        bundled_name=edk2-riscv-vars.fd
        qemu=qemu-system-riscv64
        ;;
    *) echo "Unknown firmware target: $1" >&2; exit 1 ;;
esac

qemu_bin=$(command -v "$qemu" || true)
for path in "${distro_paths[@]}" \
    "${qemu_bin%/*}/../share/qemu/$bundled_name"; do
    if [ -f "$path" ]; then
        echo "$path"
        exit 0
    fi
done

# Preserve a useful missing-file diagnostic from QEMU on unconfigured hosts.
echo "$distro_path"
