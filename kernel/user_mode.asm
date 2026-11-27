bits 64
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
    mov [rcx], rbx
    mov eax, 2
    int 0x80
    cmp rax, 1
    jbe .bad
    mov eax, 3
    int 0x80
    test rax, rax
    jz .bad
    mov rax, -1
    int 0x80
    cmp rax, -38
    jne .bad
    mov eax, 4
    int 0x80
    mov rbp, rax
.busy:
    mov rcx, 0x600000
    cmp [rcx], rbx
    jne .bad
    mov eax, 4
    int 0x80
    sub rax, rbp
    cmp rax, 17
    jb .busy
    mov eax, 1
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
section .note.GNU-stack noalloc noexec nowrite progbits
