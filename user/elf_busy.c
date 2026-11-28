#include <elf_runtime.h>
static volatile uint64_t data = 0xb057;
static volatile unsigned char bss[64];
int elf_main(const uint64_t *stack) {
    if (!elf_start_valid(stack, "busy") || data != 0xb057 || bss[0]) return 131;
    bss[0] = 1;
    static const char message[] = "ELF busy: CPU sem sleep/yield; limite 30s.\n";
    (void)sys_write(1, message, sizeof message - 1);
    uint64_t start = (uint64_t)nexus_syscall3(USER_SYSCALL_TICKS,0,0,0);
    while ((uint64_t)nexus_syscall3(USER_SYSCALL_TICKS,0,0,0) - start < 3000) ++data;
    return 0;
}
