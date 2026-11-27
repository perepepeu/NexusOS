#ifndef NEXUS_INTERRUPTS_H
#define NEXUS_INTERRUPTS_H
#include <stdint.h>
enum { KERNEL_CS = 0x08, KERNEL_DS = 0x10, USER_CS = 0x2b, USER_DS = 0x33 };

/* Ordem exata dos registros salvos por interrupt_common em Assembly. */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};

void interrupts_init(uint64_t hhdm, uint64_t kernel_physical, uint64_t kernel_virtual);
uint64_t timer_ticks(void);
void interrupts_set_kernel_stack(uint64_t top);
uint64_t interrupts_kernel_stack(void);
void timer_wait(unsigned int ticks);
__attribute__((noreturn)) void interrupts_idle(void);
void interrupt_dispatch(struct interrupt_frame *frame);
void interrupts_selftest(void);
void interrupts_enable_irq(unsigned int irq);
void interrupts_disable_irq(unsigned int irq);
__attribute__((noreturn)) void interrupts_test_double_fault(void);
#endif
