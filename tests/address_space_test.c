#include <address_space.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t frames[160][512];
static bool used[160];
static int budget = -1;
static unsigned int index_of(uint64_t pa) { if (pa >= 0x100000000ull) pa -= 0x100000000ull; return pa & 4095 ? 160 : (unsigned int)(pa / 4096); }
static uint64_t allocate(void *ctx) {
    (void)ctx; if (!budget) return 0;
    for (unsigned int i = 3; i < 160; ++i) if (!used[i]) {
        used[i] = true; memset(frames[i], 0, sizeof frames[i]); if (budget > 0) --budget;
        return 0x100000000ull + i * 4096;
    }
    return 0;
}
static void release(void *ctx, uint64_t pa) { (void)ctx; unsigned int i = index_of(pa); assert(i >= 3 && i < 160 && used[i]); used[i] = false; }
static uint64_t *table(void *ctx, uint64_t pa) { (void)ctx; unsigned int i = index_of(pa); return i < 160 && used[i] ? frames[i] : 0; }
static void invalidate(void *ctx, uint64_t va) { (void)ctx; (void)va; }
static void flush(void *ctx) { (void)ctx; }
static struct vmm_ops ops = {.allocate = allocate, .release = release, .table = table, .invalidate = invalidate, .flush = flush};
static unsigned int count(void) { unsigned int n = 0; for (unsigned int i = 0; i < 160; ++i) n += used[i]; return n; }
int main(void) {
    used[1] = used[2] = true; frames[1][0] = frames[1][511] = 0x2007; frames[2][0] = 0x87;
    uint64_t mask = 0x000ffffffffff000ull;
    struct address_space a = {0}, b = {0}; struct vmm_mapping mapping;
    budget = 0; assert(!address_core_create(&a, &ops, 0, 4096, mask) && count() == 2); budget = -1;
    assert(address_core_create(&a, &ops, 0, 4096, mask) && !address_core_create(&a, &ops, 0, 4096, mask));
    assert(!address_core_translate(&a, 4096, &mapping)); /* Mapa identidade nao e herdado. */
    assert(address_core_translate(&a, 0xffffff8000000123ull, &mapping) && mapping.physical == 0x123 && !(mapping.flags & VMM_USER));
    for (int n = 0; n < 4; ++n) {
        budget = n; unsigned int before = count();
        assert(!address_core_alloc(&a, USER_CODE_BASE, VMM_EXEC));
        assert(count() == before && a.tables == 1 && !a.pages);
    }
    budget = -1;
    uint64_t first = address_core_alloc(&a, USER_CODE_BASE, VMM_EXEC); assert(first >= 0x100000000ull);
    assert(address_core_translate(&a, USER_CODE_BASE + 7, &mapping) && mapping.flags == (VMM_EXEC | VMM_USER));
    unsigned int before = count();
    assert(!address_core_alloc(&a, USER_CODE_BASE, VMM_WRITE) && count() == before);
    assert(!address_core_alloc(&a, 0, VMM_WRITE) && !address_core_alloc(&a, USER_CODE_BASE + 1, 0));
    assert(!address_core_alloc(&a, USER_ADDRESS_END, 0) && !address_core_alloc(&a, 0xffffffff80000000ull, VMM_WRITE));
    assert(!address_core_alloc(&a, USER_DATA_BASE, VMM_WRITE | VMM_EXEC));
    assert(address_core_create(&b, &ops, 0, 4096, mask));
    uint64_t second = address_core_alloc(&b, USER_CODE_BASE, VMM_EXEC); assert(second && first != second);
    char one[] = "processo A", two[] = "processo B", result[sizeof one];
    assert(address_core_copy(&a, USER_CODE_BASE, one, sizeof one, true));
    assert(address_core_copy(&b, USER_CODE_BASE, two, sizeof two, true));
    assert(address_core_copy(&a, USER_CODE_BASE, result, sizeof result, false) && !memcmp(result, one, sizeof one));
    assert(address_core_copy(&b, USER_CODE_BASE, result, sizeof result, false) && !memcmp(result, two, sizeof two));
    assert(!address_core_copy(&a, 0xffffffff80000000ull, result, 1, false));
    assert(!address_core_copy(&a, USER_ADDRESS_END - 1, result, 2, false));
    assert(address_core_alloc(&a, USER_DATA_BASE, VMM_WRITE));
    assert(address_core_translate(&a, USER_DATA_BASE, &mapping) && mapping.flags == (VMM_USER | VMM_WRITE));
    assert(!address_core_copy(&a, USER_DATA_BASE + 4095, result, 2, true));
    assert(!address_core_destroy(&a, a.root) && a.ready);
    assert(address_core_destroy(&a, 4096) && address_core_destroy(&b, 4096) && count() == 2);
    assert(!address_core_destroy(&a, 4096));
    assert(address_core_create(&a, &ops, 0, 4096, mask));
    for (unsigned int i = 0; i < USER_SPACE_MAX_PAGES; ++i) assert(address_core_alloc(&a, USER_CODE_BASE + i * 4096, VMM_WRITE));
    before = count(); assert(!address_core_alloc(&a, USER_CODE_BASE + USER_SPACE_MAX_PAGES * 4096, 0) && count() == before);
    assert(address_core_destroy(&a, 4096) && count() == 2 && frames[1][511] == 0x2007);
    puts("OK: espacos privados, USER/supervisor, RX/NX, isolamento, limites, OOM e destruicao.");
}
