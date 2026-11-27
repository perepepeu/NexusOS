#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
config=${1:-boot/limine.conf}
output=${2:-build/nexusos.iso}

mkdir -p build
staging=$(mktemp -d build/iso-XXXXXX)
trap 'rm -rf -- "$staging"' EXIT
mkdir -p "$staging/boot/limine" "$staging/EFI/BOOT"
cp "$config" "$staging/boot/limine/limine.conf"
cp build/kernel.elf "$staging/boot/kernel.elf"
cp vendor/limine/limine-bios.sys vendor/limine/limine-bios-cd.bin \
    vendor/limine/limine-uefi-cd.bin "$staging/boot/limine/"
cp vendor/limine/BOOTX64.EFI "$staging/EFI/BOOT/"
xorriso -as mkisofs -R -r -J \
    -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
    -boot-info-table -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$staging" -o "$output"
vendor/limine/limine bios-install "$output"
