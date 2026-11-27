#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

version=12.11.1
checksum=953aba25c23abc6d6b4e85e20ec8b9f3a8605450a26997103fa3fddf00e658e6
destination=vendor/limine

if [[ -f "$destination/.version" ]] && [[ $(cat "$destination/.version") == "$version" ]]; then
    make -C "$destination"
    exit 0
fi

mkdir -p vendor
scratch=$(mktemp -d vendor/.limine-XXXXXX)
trap 'rm -rf -- "$scratch"' EXIT
curl -fL --retry 3 --connect-timeout 20 --max-time 180 \
    "https://github.com/Limine-Bootloader/Limine/releases/download/v${version}/limine-binary.tar.gz" \
    -o "$scratch/release.tar.gz"
printf '%s  %s\n' "$checksum" "$scratch/release.tar.gz" | sha256sum -c -
tar -xzf "$scratch/release.tar.gz" -C "$scratch"
make -C "$scratch/limine-binary"
if [[ -e "$destination" ]]; then
    echo 'vendor/limine ja existe com outra versao; remova ou mova esse diretorio antes de continuar.' >&2
    exit 1
fi
printf '%s\n' "$version" > "$scratch/limine-binary/.version"
mv "$scratch/limine-binary" "$destination"
