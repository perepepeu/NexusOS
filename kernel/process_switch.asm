bits 64
section .text
global process_context_switch
; SysV: RDI = endereco do RSP salvo do anterior, RSI = novo RSP.
; Chamador mantem IRQs desabilitadas durante a troca.
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
section .note.GNU-stack noalloc noexec nowrite progbits
