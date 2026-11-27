bits 64
section .text
extern interrupt_dispatch
global gdt_load

; RDI aponta para GDTR; recarrega CS por retorno distante e carrega TR.
gdt_load:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .segments]
    push rax
    retfq
.segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov ax, 0x18
    ltr ax
    ret

%assign vector 0
%rep 256
global isr_%+vector
isr_%+vector:
%if vector != 8 && vector != 10 && vector != 11 && vector != 12 && vector != 13 && vector != 14 && vector != 17 && vector != 21 && vector != 29 && vector != 30
    push qword 0
%endif
    push qword vector
    jmp interrupt_common
%assign vector vector+1
%endrep

interrupt_common:
    ; Frame completo permanece nesta pilha durante preempcao. A tarefa que
    ; retoma este caminho recupera GPRs e RFLAGS originais via IRETQ.
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp
    mov rbx, rsp
    and rsp, -16
    call interrupt_dispatch
    mov rsp, rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

; Confere registros de chamada e callee-saved, DF e retorno por IRETQ.
global interrupt_register_test
interrupt_register_test:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rax, 1
    mov rbx, 2
    mov rcx, 3
    mov rdx, 4
    mov rsi, 5
    mov rdi, 6
    mov rbp, 7
    mov r8, 8
    mov r9, 9
    mov r10, 10
    mov r11, 11
    mov r12, 12
    mov r13, 13
    mov r14, 14
    mov r15, 15
    std
    int3
    pushfq
    test qword [rsp], 0x400
    lea rsp, [rsp+8]
    jz .failed
    cld
%macro CHECK_REG 2
    cmp %1, %2
    jne .failed
%endmacro
    CHECK_REG rax, 1
    CHECK_REG rbx, 2
    CHECK_REG rcx, 3
    CHECK_REG rdx, 4
    CHECK_REG rsi, 5
    CHECK_REG rdi, 6
    CHECK_REG rbp, 7
    CHECK_REG r8, 8
    CHECK_REG r9, 9
    CHECK_REG r10, 10
    CHECK_REG r11, 11
    CHECK_REG r12, 12
    CHECK_REG r13, 13
    CHECK_REG r14, 14
    CHECK_REG r15, 15
    mov eax, 1
    jmp .done
.failed:
    cld
    xor eax, eax
.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

section .rodata
align 8
global isr_table
isr_table:
%assign vector 0
%rep 256
    dq isr_%+vector
%assign vector vector+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
