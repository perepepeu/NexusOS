bits 64
section .text
global _start
extern kmain

_start:
    cli
    cld
    lea rsp, [rel stack_top]
    test rsp, 15
    jnz .halt
    xor ebp, ebp
    call kmain
.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
