#ifndef NEXUS_USER_MODE_H
#define NEXUS_USER_MODE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <interrupts.h>
#include <syscall_abi.h>
/* Imagem plana fornecida pelo kernel, ate 64 KiB, sem ELF/dinamico/fork. */
uint64_t process_create_user(const char *name, const void *image, size_t bytes, size_t entry_offset, uint64_t argument);
bool process_user_syscall(struct interrupt_frame *frame);
bool process_user_fault(struct interrupt_frame *frame, uint64_t cr2);
void process_user_return(struct interrupt_frame *frame);
void user_mode_selftest(void);
void user_mode_fault_selftest(void);
void syscall_selftest(void);
uint64_t process_user_demo(void);
#endif
