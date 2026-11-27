bits 64
%include "syscall_abi.inc"
section .text
global user_mode_enter
; RDI=RIP, RSI=RSP de usuario, RDX=argumento escalar. Nunca retorna.
user_mode_enter:
    cli
    mov ax, 0x33
    mov ds, ax
    mov es, ax
    push qword 0x33
    push rsi
    push qword 0x202 ; IF=1, IOPL=0, DF/TF/NT=0.
    push qword 0x2b
    push rdi
    mov rdi, rdx
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

; Payload copiado para 0x400000. Referencias de controle sao relativas.
section .rodata
global user_valid_start, user_valid_end
user_valid_start:
    mov ax, cs
    and eax, 3
    cmp eax, 3
    jne .bad
    mov ax, ss
    and eax, 3
    cmp eax, 3
    jne .bad
    pushfq
    pop rax
    test eax, 0x3000
    jnz .bad
    mov rbx, rdi
    push rbx
    pop rdx
    cmp rdx, rbx
    jne .bad
    mov rcx, 0x600000
    cmp qword [rcx], 0 ; Paginas novas nao podem revelar dados de um PID anterior.
    jne .bad
    mov [rcx], rbx
    mov eax, USER_SYSCALL_GETPID
    int 0x80
    cmp rax, 1
    jbe .bad
    mov eax, USER_SYSCALL_GETTID
    int 0x80
    test rax, rax
    jz .bad
    mov rax, -1
    int 0x80
    cmp rax, -USER_ENOSYS
    jne .bad
    mov eax, USER_SYSCALL_TICKS
    int 0x80
    mov rbp, rax
.busy:
    mov rcx, 0x600000
    cmp [rcx], rbx
    jne .bad
    mov eax, USER_SYSCALL_TICKS
    int 0x80
    sub rax, rbp
    cmp rax, 17
    jb .busy
    mov eax, USER_SYSCALL_YIELD
    int 0x80
    test rax, rax
    jnz .bad
    mov rdi, rbx
    xor eax, eax
    int 0x80
    ud2
.bad:
    mov edi, 99
    xor eax, eax
    int 0x80
    ud2
user_valid_end:

; Cada imagem e independente, copiada para USER_CODE_BASE pelo loader de teste.
%macro PROBE 1
global user_probe_%1_start, user_probe_%1_end
user_probe_%1_start:
%endmacro
%macro END_PROBE 1
    mov edi, 99 ; So alcancado se a CPU nao bloquear a operacao invalida.
    xor eax, eax
    int 0x80
    ud2
user_probe_%1_end:
%endmacro

PROBE read
    mov al, [rdi]
END_PROBE read
PROBE write
    mov byte [rdi], 1
END_PROBE write
PROBE execute
    jmp rdi
END_PROBE execute
PROBE textwrite
    mov rax, 0x400000
    mov byte [rax], 0x90
END_PROBE textwrite
PROBE nx
    mov rax, 0x600000
    mov byte [rax], 0xc3
    call rax
END_PROBE nx
PROBE cli
    cli
END_PROBE cli
PROBE halt
    hlt
END_PROBE halt
PROBE port
    mov dx, 0x80
    out dx, al
END_PROBE port
PROBE intgate
    int 0
END_PROBE intgate
PROBE divide
    xor ecx, ecx
    xor edx, edx
    mov eax, 1
    div ecx
END_PROBE divide
PROBE opcode
    std ; O handler precisa limpar DF antes de entrar em C.
    ud2
END_PROBE opcode
PROBE fpu
    fninit
END_PROBE fpu
PROBE syscall
    syscall
END_PROBE syscall
PROBE cr3
    mov rax, cr3
END_PROBE cr3
PROBE msr
    mov ecx, 0xc0000080
    rdmsr
END_PROBE msr
PROBE iret
    push qword 0x10
    push rsp
    push qword 0x202
    push qword 0x08
    push qword 0x400000
    iretq
END_PROBE iret
PROBE badstack
    xor esp, esp
    ud2 ; A excecao deve usar RSP0, nunca a pilha de usuario invalida.
END_PROBE badstack
PROBE badreturn
    mov rsp, 0x0100000000000000
    mov eax, USER_SYSCALL_GETPID
    int 0x80
END_PROBE badreturn
PROBE badirq
    mov rsp, 0x0100000000000000
    jmp $ ; O PIT precisa enviar EOI antes de encerrar este processo.
END_PROBE badirq
section .note.GNU-stack noalloc noexec nowrite progbits
