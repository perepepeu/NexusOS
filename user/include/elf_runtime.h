#ifndef NEXUS_ELF_RUNTIME_H
#define NEXUS_ELF_RUNTIME_H
#include <syscall.h>
extern void _start(void);
static inline int elf_start_valid(const uint64_t *stack, const char *name) {
    uint16_t cs; __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    if ((cs & 3) != 3 || ((uintptr_t)stack & 15) || stack[0] != 1 || stack[2] || stack[3] ||
        stack[4] != 6 || stack[5] != 4096 || stack[6] != 9 || stack[7] != (uintptr_t)_start || stack[8] || stack[9]) return 0;
    if (stack[1] < 0x800000 || stack[1] >= 0x803ff8) return 0;
    const char *arg0 = (const char *)(uintptr_t)stack[1];
    for (unsigned int i = 0; i < 24; ++i) {
        if (arg0[i] != name[i]) return 0;
        if (!name[i]) return 1;
    }
    return 0;
}
#endif
