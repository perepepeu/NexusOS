#!/usr/bin/env bash
set -euo pipefail

cc=$1
assembler=$2
linker=$3
arch=$4
read -r -a cflags <<< "$5"
read -r -a asflags <<< "$6"
read -r -a ldflags <<< "$7"

scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT

cat > "$scratch/test.c" <<'C'
unsigned int nexus_add(unsigned int a, unsigned int b) {
    return a + b;
}
C

if [[ "$arch" == x86_64 ]]; then
    cat > "$scratch/start.asm" <<'ASM'
bits 64
section .text
global _start
extern nexus_add
_start:
    mov edi, 2
    mov esi, 3
    call nexus_add
.halt:
    hlt
    jmp .halt
section .note.GNU-stack noalloc noexec nowrite progbits
ASM
else
    cat > "$scratch/start.asm" <<'ASM'
bits 32
section .text
global _start
extern nexus_add
_start:
    push dword 3
    push dword 2
    call nexus_add
    add esp, 8
.halt:
    hlt
    jmp .halt
section .note.GNU-stack noalloc noexec nowrite progbits
ASM
fi

"$cc" "${cflags[@]}" -c "$scratch/test.c" -o "$scratch/test.o"
"$assembler" "${asflags[@]}" "$scratch/start.asm" -o "$scratch/start.o"
"$linker" "${ldflags[@]}" -e _start -Ttext=0x100000 "$scratch/start.o" "$scratch/test.o" -o "$scratch/test.elf"
test -z "$(nm -u "$scratch/test.elf")"
readelf -h "$scratch/test.elf" | sed -n '/Class:/p; /Machine:/p; /Entry point/p'
printf 'OK: C e Assembly compilados e vinculados para %s, sem bibliotecas do Linux.\n' "$arch"
