#include <elf64.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char image[0x3000];
static struct elf64_header h;
static struct elf64_phdr p[3];
static uint64_t frames[180][512];
static bool used[180];
static int budget = -1;
static const uint64_t mask = 0x000ffffffffff000ull;
static unsigned int index_of(uint64_t pa) {
    if (pa >= 0x100000000ull) pa -= 0x100000000ull;
    return (pa & 4095) || pa / 4096 >= 180 ? 180 : (unsigned int)(pa / 4096);
}
static uint64_t allocate(void *ctx) {
    (void)ctx; if (!budget) return 0;
    for (unsigned int i = 3; i < 180; ++i) if (!used[i]) {
        used[i] = true; memset(frames[i], 0, sizeof frames[i]); if (budget > 0) --budget;
        return 0x100000000ull + i * 4096;
    }
    return 0;
}
static void release(void *ctx, uint64_t pa) {
    (void)ctx; unsigned int i = index_of(pa); assert(i >= 3 && i < 180 && used[i]); used[i] = false;
}
static uint64_t *table(void *ctx, uint64_t pa) { (void)ctx; unsigned int i = index_of(pa); return i < 180 && used[i] ? frames[i] : 0; }
static void invalidate(void *ctx, uint64_t va) { (void)ctx; (void)va; }
static void flush(void *ctx) { (void)ctx; }
static struct vmm_ops ops = {.allocate = allocate, .release = release, .table = table, .invalidate = invalidate, .flush = flush};
static unsigned int count(void) { unsigned int n = 0; for (unsigned int i = 0; i < 180; ++i) n += used[i]; return n; }
static void seal(void) { memcpy(image, &h, sizeof h); memcpy(image + sizeof h, p, h.phnum <= 3 ? h.phnum * sizeof *p : sizeof p); }
static void reset(void) {
    memset(image, 0xcc, sizeof image);
    h = (struct elf64_header){.ident = {0x7f,'E','L','F',2,1,1}, .type = 2, .machine = 62, .version = 1,
        .entry = 0x401003, .phoff = 64, .ehsize = 64, .phentsize = 56, .phnum = 2};
    p[0] = (struct elf64_phdr){.type = 1, .flags = 5, .offset = 0x1000, .vaddr = 0x401000, .paddr = 4096, .filesz = 17, .memsz = 4096, .align = 4096};
    p[1] = (struct elf64_phdr){.type = 1, .flags = 6, .offset = 0x2000, .vaddr = 0x600000, .paddr = 4096, .filesz = 8, .memsz = 8192, .align = 4096};
    p[2] = (struct elf64_phdr){.type = 1, .flags = 4, .offset = 0x2800, .vaddr = 0x602800, .filesz = 32, .memsz = 40, .align = 4096};
    for (unsigned int i = 0; i < 17; ++i) image[0x1000 + i] = 0x90;
    uint64_t value = 0xabcdef0123456789ull; memcpy(image + 0x2000, &value, sizeof value); seal();
}
static void bad(enum elf64_result expected) {
    seal(); struct elf64_plan plan;
    assert(elf64_validate(image, sizeof image, &plan) == expected && !plan.count);
    struct address_space space = {0}; uint64_t entry = 1, stack = 1;
    assert(elf64_build(&space, &ops, 0, 4096, mask, image, sizeof image, "test", &entry, &stack) == expected);
    assert(!space.ready && !entry && !stack && count() == 2);
}
#define BAD(change, expected) do { reset(); change; bad(expected); } while (0)
int main(void) {
    used[1] = used[2] = true; frames[1][511] = 0x2003; frames[2][0] = 0x83;
    BAD(h.ident[0] = 0, ELF64_HEADER);
    BAD(h.ident[4] = 1, ELF64_UNSUPPORTED); BAD(h.ident[5] = 2, ELF64_UNSUPPORTED);
    BAD(h.ident[6] = 0, ELF64_UNSUPPORTED); BAD(h.ident[7] = 3, ELF64_UNSUPPORTED);
    BAD(h.ident[8] = 1, ELF64_UNSUPPORTED); BAD(h.type = 3, ELF64_UNSUPPORTED);
    BAD(h.machine = 3, ELF64_UNSUPPORTED); BAD(h.version = 0, ELF64_UNSUPPORTED);
    BAD(h.flags = 1, ELF64_UNSUPPORTED); BAD(h.ehsize = 63, ELF64_HEADER);
    BAD(h.phentsize = 55, ELF64_HEADER); BAD(h.phnum = 0, ELF64_HEADER);
    BAD(h.phnum = 33, ELF64_LIMIT); BAD(h.phoff = UINT64_MAX, ELF64_TRUNCATED);
    BAD(h.phoff = 0, ELF64_TRUNCATED); BAD(h.phoff = sizeof image - 1, ELF64_TRUNCATED);
    BAD(h.shoff = UINT64_MAX; h.shnum = 1; h.shentsize = 64, ELF64_HEADER);
    BAD(h.shoff = 64; h.shnum = 1; h.shentsize = 64; h.shstrndx = 2, ELF64_HEADER);
    BAD(h.shoff = 64, ELF64_UNSUPPORTED);
    BAD(p[0].offset = UINT64_MAX, ELF64_TRUNCATED);
    BAD(p[0].filesz = p[0].memsz + 1, ELF64_SEGMENT);
    BAD(p[1].memsz = UINT64_MAX, ELF64_SEGMENT);
    BAD(p[1].vaddr = 0xffffffff80000000ull, ELF64_SEGMENT);
    BAD(p[1].vaddr = 0, ELF64_SEGMENT);
    BAD(p[1].vaddr = USER_ADDRESS_END - 4096, ELF64_SEGMENT);
    BAD(p[1].flags = 7, ELF64_PERMISSIONS); BAD(p[1].flags = 2, ELF64_PERMISSIONS);
    BAD(p[1].flags = 0x80000006u, ELF64_PERMISSIONS);
    BAD(p[0].vaddr++, ELF64_SEGMENT); BAD(p[0].align = 3, ELF64_SEGMENT);
    BAD(p[0].align = 8192; p[0].vaddr = 0x402000, ELF64_SEGMENT);
    BAD(p[1].vaddr = USER_STACK_BASE, ELF64_OVERLAP);
    BAD(p[1].vaddr = USER_STACK_BASE - 4096, ELF64_OVERLAP);
    BAD(p[1].vaddr = USER_STACK_TOP, ELF64_OVERLAP);
    BAD(p[1].vaddr = 0x401800; p[1].offset = 0x2800, ELF64_OVERLAP);
    BAD(p[1].memsz = 61 * 4096, ELF64_LIMIT);
    BAD(h.entry = 0x600000, ELF64_ENTRY); BAD(h.entry = 0x401011, ELF64_ENTRY);
    BAD(h.entry = USER_ADDRESS_END, ELF64_ENTRY); BAD(p[0].flags = 4, ELF64_ENTRY);
    BAD(p[1].type = 2, ELF64_UNSUPPORTED); BAD(p[1].type = 3, ELF64_UNSUPPORTED);
    BAD(p[1].type = 7, ELF64_UNSUPPORTED); BAD(p[1].type = 0x12345678, ELF64_UNSUPPORTED);
    BAD(p[1].type = 0x6474e551; p[1].flags = 1, ELF64_PERMISSIONS);
    reset(); struct elf64_plan plan;
    assert(elf64_validate(image, sizeof image, &plan) == ELF64_OK && plan.entry == h.entry && plan.count == 2 && plan.pages == 3);
    assert(elf64_validate(image, ELF64_MAX_FILE + 1, &plan) == ELF64_LIMIT);
    assert(elf64_validate(0, 0, &plan) == ELF64_ARGUMENT && elf64_validate(image, sizeof image, 0) == ELF64_ARGUMENT);
    const size_t cuts[] = {0,1,63,64,119,175,4095,4096,4112,8191,8192,8199,8200,sizeof image};
    for (size_t i = 0; i < sizeof cuts / sizeof *cuts; ++i) {
        size_t bytes = cuts[i]; unsigned char *input = malloc(bytes ? bytes : 1); assert(input);
        memcpy(input, image, bytes);
        assert(elf64_validate(input, bytes, &plan) == (bytes < 8200 ? ELF64_TRUNCATED : ELF64_OK)); free(input);
    }
    unsigned char unaligned[sizeof image + 1]; memcpy(unaligned + 1, image, sizeof image);
    assert(elf64_validate(unaligned + 1, sizeof image, &plan) == ELF64_OK);
    struct address_space a = {0}, b = {0}; uint64_t entry, stack;
    assert(elf64_build(&a, &ops, 0, 4096, mask, image, sizeof image, "alpha", &entry, &stack) == ELF64_OK);
    assert(entry == 0x401003 && !(stack & 15) && a.pages == 7);
    struct vmm_mapping mapping, other;
    assert(address_core_translate(&a, entry, &mapping) && mapping.flags == (VMM_USER | VMM_EXEC));
    assert(address_core_translate(&a, 0x600000, &mapping) && mapping.flags == (VMM_USER | VMM_WRITE) && mapping.physical >= 0x100000000ull);
    uint64_t data; assert(address_core_copy(&a, 0x600000, &data, sizeof data, false) && data == 0xabcdef0123456789ull);
    unsigned char zero[8192 - 8]; memset(zero, 0xff, sizeof zero);
    assert(address_core_copy(&a, 0x600008, zero, sizeof zero, false));
    for (size_t i = 0; i < sizeof zero; ++i) assert(!zero[i]);
    uint64_t words[10]; char name[6];
    assert(address_core_copy(&a, stack, words, sizeof words, false));
    assert(words[0] == 1 && !words[2] && !words[3] && words[4] == 6 && words[5] == 4096 && words[6] == 9 && words[7] == entry && !words[8] && !words[9]);
    assert(address_core_copy(&a, words[1], name, sizeof name, false) && !strcmp(name, "alpha"));
    assert(address_core_translate(&a, stack, &mapping) && mapping.flags == (VMM_USER | VMM_WRITE));
    assert(!address_core_translate(&a, USER_STACK_BASE - 1, &mapping) && !address_core_translate(&a, USER_STACK_TOP, &mapping));
    assert(!address_core_user_check(&a, entry, 1, true));
    assert(elf64_build(&b, &ops, 0, 4096, mask, image, sizeof image, "beta", &entry, &stack) == ELF64_OK);
    assert(a.root != b.root && address_core_translate(&a, 0x600000, &mapping) && address_core_translate(&b, 0x600000, &other) && mapping.physical != other.physical);
    data = 17; assert(address_core_user_copy(&a, 0x600000, &data, sizeof data, true));
    assert(address_core_user_copy(&b, 0x600000, &data, sizeof data, false) && data == 0xabcdef0123456789ull);
    unsigned int allocated = count();
    assert(elf64_build(&a, &ops, 0, 4096, mask, image, sizeof image, "again", &entry, &stack) == ELF64_ARGUMENT && count() == allocated);
    assert(address_core_destroy(&a, 4096) && address_core_destroy(&b, 4096) && count() == 2);
    /* Toda posicao de falha de alocacao antes do primeiro sucesso. */
    bool success = false;
    for (int i = 0; i < 40 && !success; ++i) {
        budget = i; enum elf64_result result = elf64_build(&a, &ops, 0, 4096, mask, image, sizeof image, "oom", &entry, &stack);
        if (result == ELF64_OK) { success = true; assert(address_core_destroy(&a, 4096)); }
        else assert(result == ELF64_MEMORY && !a.ready && !entry && !stack);
        assert(count() == 2);
    }
    assert(success); budget = -1;
    reset(); h.phnum = 3; seal();
    assert(elf64_build(&a, &ops, 0, 4096, mask, image, sizeof image, "ro", &entry, &stack) == ELF64_OK);
    assert(address_core_translate(&a, 0x602800, &mapping) && mapping.flags == VMM_USER);
    assert(address_core_destroy(&a, 4096));
    reset(); p[0].vaddr += 3; p[0].offset += 3; p[0].align = 1; seal();
    assert(elf64_build(&a, &ops, 0, 4096, mask, image, sizeof image, "unaligned", &entry, &stack) == ELF64_OK);
    unsigned char prefix[3] = {1,1,1}; assert(address_core_copy(&a, 0x401000, prefix, sizeof prefix, false));
    assert(!prefix[0] && !prefix[1] && !prefix[2] && address_core_destroy(&a, 4096));
    reset(); h.phnum = 1; p[0].vaddr = h.entry = USER_ADDRESS_END - 4096; seal();
    assert(elf64_build(&a, &ops, 0, 4096, mask, image, sizeof image, "high", &entry, &stack) == ELF64_OK && address_core_destroy(&a, 4096));
    reset(); uint32_t rng = 0xaca01234;
    for (unsigned int i = 0; i < 10000; ++i) {
        memcpy(unaligned, image, sizeof image);
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        unaligned[rng % 256] ^= (unsigned char)(rng >> 16);
        size_t bytes = (i % 3) ? sizeof image : rng % sizeof image;
        if (elf64_validate(unaligned, bytes, &plan) == ELF64_OK) assert(plan.count && plan.count <= ELF64_MAX_LOAD && plan.pages <= USER_SPACE_MAX_PAGES - 4);
    }
    assert(count() == 2 && frames[1][511] == 0x2003);
    puts("OK: ELF64, validacao hostil, PT_LOAD/RX/RO/RW-NX, BSS, stack/argv, isolamento, OOM e 10000 mutacoes.");
}
