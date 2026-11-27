bits 64
section .text
global process_context_switch
; SysV: RDI = endereco do RSP salvo do anterior, RSI = novo RSP.
; Chamador mantem IRQs desabilitadas durante a troca. Em preempcao o frame
; completo do interrupt_common fica abaixo deste contexto e retorna por IRETQ.
process_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

extern process_yield
extern process_sleep
global process_register_test
process_register_test:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov rbp, 0x11112222
    mov rbx, 0x33334444
    mov r12, 0x55556666
    mov r13, 0x77778888
    mov r14, 0x12345678
    mov r15, 0x76543210
    call process_yield
    mov edi, 2
    call process_sleep
    xor eax, eax
    cmp rbp, 0x11112222
    jne .done
    cmp rbx, 0x33334444
    jne .done
    mov ecx, 0x55556666
    cmp r12, rcx
    jne .done
    mov ecx, 0x77778888
    cmp r13, rcx
    jne .done
    cmp r14, 0x12345678
    jne .done
    cmp r15, 0x76543210
    jne .done
    mov eax, 1
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
extern scheduler_probe_a
extern scheduler_probe_b
extern scheduler_probe_stop
global process_preempt_register_test
process_preempt_register_test:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 24
    mov [rsp], rdi
    mov qword [rsp+16], 0x55aa8877
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
.spin:
    cmp byte [rel scheduler_probe_stop], 0
    jne .check
    cmp qword [rsp], 0
    jne .second
    inc qword [rel scheduler_probe_a]
    jmp .again
.second:
    inc qword [rel scheduler_probe_b]
.again:
    ; Janela com flags conhecidas: CF/PF/ZF/IF/DF e demais bits zerados.
    push qword 0x647
    popfq
    times 128 nop
    pushfq
    cmp qword [rsp], 0x647
    lea rsp, [rsp+8]
    jne .bad
    pause
    jmp .spin
.check:
    pushfq
    test qword [rsp], 0x400
    lea rsp, [rsp+8]
    jz .bad
    pushfq
    test qword [rsp], 0x200
    lea rsp, [rsp+8]
    jz .bad
    cld
%macro PREEMPT_CHECK 2
    cmp %1, %2
    jne .bad
%endmacro
    PREEMPT_CHECK rax, 1
    PREEMPT_CHECK rbx, 2
    PREEMPT_CHECK rcx, 3
    PREEMPT_CHECK rdx, 4
    PREEMPT_CHECK rsi, 5
    PREEMPT_CHECK rdi, 6
    PREEMPT_CHECK rbp, 7
    PREEMPT_CHECK r8, 8
    PREEMPT_CHECK r9, 9
    PREEMPT_CHECK r10, 10
    PREEMPT_CHECK r11, 11
    PREEMPT_CHECK r12, 12
    PREEMPT_CHECK r13, 13
    PREEMPT_CHECK r14, 14
    PREEMPT_CHECK r15, 15
    cmp qword [rsp+16], 0x55aa8877
    jne .bad
    mov eax, 1
    jmp .end
.bad:
    cld
    xor eax, eax
.end:
    add rsp, 24
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
