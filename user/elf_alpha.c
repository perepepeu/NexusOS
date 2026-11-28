#include <elf_runtime.h>
static volatile uint64_t data = 0xa1a2a3a4a5a6a7a8ull;
static volatile unsigned char bss[6000];
int elf_main(const uint64_t *stack) {
    if (!elf_start_valid(stack, "alpha") || data != 0xa1a2a3a4a5a6a7a8ull) return 101;
    for (size_t i = 0; i < sizeof bss; ++i) if (bss[i]) return 102;
    for (size_t i = 0; i < sizeof bss; ++i) bss[i] = 0xa1;
    static const char start[] = "ELF alpha: inicio CPL3, data/BSS OK\n";
    if (sys_write(1, start, sizeof start - 1) != sizeof start - 1) return 103;
    for (uint64_t i = 0; i < 8; ++i) {
        data = 0xa1a2a3a4a5a6a7a8ull ^ i;
        if (sys_sleep(80) || data != (0xa1a2a3a4a5a6a7a8ull ^ i) || bss[0] != 0xa1 || bss[4095] != 0xa1 || bss[5999] != 0xa1) return 104;
    }
    static const char done[] = "ELF alpha: fim isolado\n";
    (void)sys_write(1, done, sizeof done - 1);
    return 0;
}
