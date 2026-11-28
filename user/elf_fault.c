#include <elf_runtime.h>
static volatile uint64_t data = 0xfa017;
static volatile unsigned char bss[64];
int elf_main(const uint64_t *stack) {
    if (!elf_start_valid(stack, "fault") || data != 0xfa017 || bss[0]) return 121;
    bss[0] = 1;
    static const char message[] = "ELF fault: aplicativo de teste vai acessar NULL.\n";
    (void)sys_write(1, message, sizeof message - 1);
    (void)sys_sleep(20);
    __asm__ volatile ("xor %%rax, %%rax; mov (%%rax), %%rax" : : : "rax", "memory");
    return 122;
}
