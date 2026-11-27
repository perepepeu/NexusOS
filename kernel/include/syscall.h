#ifndef NEXUS_KERNEL_SYSCALL_H
#define NEXUS_KERNEL_SYSCALL_H
#include <address_space.h>
#include <interrupts.h>
#include <syscall_abi.h>
/* Somente o dispatcher validado de CPL3, em contexto de tarefa com IF=1.
 * Nenhum destes ponteiros de usuario e dereferenciado diretamente. */
int64_t sys_write(struct address_space *, uint64_t fd, uint64_t buffer, uint64_t bytes);
int64_t sys_read(struct address_space *, uint64_t fd, uint64_t buffer, uint64_t bytes);
int64_t sys_sleep(uint64_t milliseconds);
int64_t sys_yield(void);
int64_t sys_getpid(void);
__attribute__((noreturn)) void sys_exit(uint64_t status);
int64_t syscall_dispatch(struct address_space *, const struct interrupt_frame *);
#endif
