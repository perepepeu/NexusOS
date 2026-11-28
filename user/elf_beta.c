#include <elf_runtime.h>
static volatile uint64_t data = 0xb1b2b3b4b5b6b7b8ull;
static volatile unsigned char bss[6000];
int elf_main(const uint64_t *stack) {
    if (!elf_start_valid(stack, "beta") || data != 0xb1b2b3b4b5b6b7b8ull) return 111;
    for (size_t i = 0; i < sizeof bss; ++i) if (bss[i]) return 112;
    for (size_t i = 0; i < sizeof bss; ++i) bss[i] = 0xb2;
    static const char start[] = "ELF beta: inicio CPL3, data/BSS OK\n";
    if (sys_write(1, start, sizeof start - 1) != sizeof start - 1) return 113;
    for (uint64_t i = 0; i < 8; ++i) {
        data = 0xb1b2b3b4b5b6b7b8ull + i;
        if (sys_sleep(50) || data != 0xb1b2b3b4b5b6b7b8ull + i || bss[0] != 0xb2 || bss[4095] != 0xb2 || bss[5999] != 0xb2) return 114;
    }
    static const char done[] = "ELF beta: fim isolado\n";
    (void)sys_write(1, done, sizeof done - 1);
    return 0;
}
