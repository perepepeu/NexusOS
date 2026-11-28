bits 64
%include "syscall_abi.inc"
section .text.entry
global _start
extern elf_main
_start:
    mov rdi, rsp ; argc/argv/envp/auxv originais.
    and rsp, -16
    call elf_main
    mov edi, eax
    mov eax, USER_SYSCALL_EXIT
    int 0x80
    ud2
section .note.GNU-stack noalloc noexec nowrite progbits
