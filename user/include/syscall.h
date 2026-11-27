#ifndef NEXUS_USER_SYSCALL_H
#define NEXUS_USER_SYSCALL_H
#include <stdint.h>
#include <stddef.h>
#include <syscall_abi.h>
/* Sem libc/errno global: retorno negativo e o proprio erro. */
static inline int64_t nexus_syscall3(uint64_t number, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "0"(number), "D"(a), "S"(b), "d"(c) : "memory", "cc");
    return (int64_t)result;
}
static inline int64_t sys_write(uint64_t fd, const void *buffer, size_t bytes) {
    return nexus_syscall3(USER_SYSCALL_WRITE, fd, (uintptr_t)buffer, bytes);
}
static inline int64_t sys_read(uint64_t fd, void *buffer, size_t bytes) {
    return nexus_syscall3(USER_SYSCALL_READ, fd, (uintptr_t)buffer, bytes);
}
static inline int64_t sys_getpid(void) { return nexus_syscall3(USER_SYSCALL_GETPID, 0, 0, 0); }
static inline int64_t sys_yield(void) { return nexus_syscall3(USER_SYSCALL_YIELD, 0, 0, 0); }
static inline int64_t sys_sleep(uint64_t milliseconds) { return nexus_syscall3(USER_SYSCALL_SLEEP, milliseconds, 0, 0); }
__attribute__((noreturn)) static inline void sys_exit(int status) {
    (void)nexus_syscall3(USER_SYSCALL_EXIT, (uint32_t)status, 0, 0);
    for (;;) __asm__ volatile ("ud2");
}
#endif
