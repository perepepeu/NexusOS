bits 64
%include "syscall_abi.inc"
section .text
global user_abi_register_test
; Retorna 1 se GPRs e CF/DF/IF atravessam yield e sleep sem corrupcao.
user_abi_register_test:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
%macro CHECK_TRAP 2
    mov eax, %1
    mov edi, %2
    mov esi, 0x1002
    mov edx, 0x1003
    mov ecx, 0x1004
    mov ebx, 0x1005
    mov ebp, 0x1006
    mov r8d, 0x1008
    mov r9d, 0x1009
    mov r10d, 0x1010
    mov r11d, 0x1011
    mov r12d, 0x1012
    mov r13d, 0x1013
    mov r14d, 0x1014
    mov r15d, 0x1015
    std
    stc
    int 0x80
    pushfq
    cld
    cmp rax, 0
    jne %%bad
    cmp rdi, %2
    jne %%bad
    cmp rsi, 0x1002
    jne %%bad
    cmp rdx, 0x1003
    jne %%bad
    cmp rcx, 0x1004
    jne %%bad
    cmp rbx, 0x1005
    jne %%bad
    cmp rbp, 0x1006
    jne %%bad
%assign register 8
%rep 8
%if register < 10
    cmp r%+register, 0x1000 + register
%else
    cmp r%+register, 0x1010 + register - 10
%endif
    jne %%bad
%assign register register+1
%endrep
    pop rax
    and eax, 0x601
    cmp eax, 0x601
    jne .fail
    jmp %%done
%%bad:
    add rsp, 8
    jmp .fail
%%done:
%endmacro
    CHECK_TRAP USER_SYSCALL_YIELD, 0x1001
    CHECK_TRAP USER_SYSCALL_SLEEP, 20
    mov eax, 1
    jmp .return
.fail:
    xor eax, eax
.return:
    cld
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
