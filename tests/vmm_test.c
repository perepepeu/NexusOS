#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vmm_internal.h>

static uint64_t frames[64][512];
static bool used[64];
static int budget = -1;
static unsigned int flushes, invalidations;
static unsigned int index_of(uint64_t physical) {
    if (physical >= 0x100000000ull) physical -= 0x100000000ull;
    return physical % 4096 ? 64 : physical / 4096;
}
static uint64_t allocate(void *ctx) {
    (void)ctx;
    if (budget == 0) return 0;
    for (unsigned int i = 1; i < 64; ++i) if (!used[i]) {
        used[i] = true; memset(frames[i], 0, sizeof frames[i]);
        if (budget > 0) --budget;
        return 0x100000000ull + i * 4096;
    }
    return 0;
}
static void release(void *ctx, uint64_t physical) {
    (void)ctx;
    unsigned int i = index_of(physical);
    assert(physical >= 0x100000000ull && i < 64 && used[i] && i != 1 && i != 3 && i != 4);
    used[i] = false;
}
static uint64_t *table(void *ctx, uint64_t physical) {
    (void)ctx;
    unsigned int i = index_of(physical);
    return i < 64 && used[i] ? frames[i] : 0;
}
static bool allocated(void *ctx, uint64_t physical) {
    unsigned int i = index_of(physical);
    return physical >= 0x100000000ull && i != 1 && i != 3 && i != 4 && table(ctx, physical) != 0;
}
static void invalidate(void *ctx, uint64_t va) { (void)ctx; (void)va; ++invalidations; }
static void flush(void *ctx) { (void)ctx; ++flushes; }
static unsigned int count(void) {
    unsigned int result = 0;
    for (unsigned int i = 0; i < 64; ++i) result += used[i];
    return result;
}

int main(void) {
    used[1] = used[3] = used[4] = true;
    frames[1][0] = 3 * 4096 | 3;
    frames[3][0] = 0x40000000ull | 0x83 | (1ull << 63);
    frames[3][1] = 4 * 4096 | 3;
    frames[4][0] = 0x80000000ull | 0x83;
    uint64_t inherited[512]; memcpy(inherited, frames[1], sizeof inherited);
    struct vmm_ops ops = {allocate, release, table, allocated, invalidate, flush};
    struct vmm_state state = {0};
    budget = 0;
    assert(vmm_core_init(&state, &ops, 0, 4096, 36) == VMM_NO_MEMORY && count() == 3);
    budget = -1;
    assert(vmm_core_init(&state, &ops, 0, 4096, 36) == VMM_OK);
    assert(vmm_core_init(&state, &ops, 0, 4096, 36) == VMM_ALREADY_INITIALIZED);
    assert(count() == 4 && state.stats.table_pages == 1);
    struct vmm_mapping mapping;
    assert(vmm_core_translate(&state, 123, &mapping) && mapping.physical == 0x4000007bull);
    assert(mapping.page_size == 1ull << 30 && mapping.flags == VMM_WRITE);
    assert(vmm_core_translate(&state, (1ull << 30) + 321, &mapping) && mapping.physical == 0x80000141ull);
    assert(mapping.page_size == 1ull << 21 && mapping.flags == (VMM_WRITE | VMM_EXEC));
    uint64_t *copy = table(0, state.stats.root_address);
    copy[0] &= ~2ull;
    assert(vmm_core_translate(&state, 123, &mapping) && mapping.flags == 0);
    copy[0] |= 2ull;
    copy[0] |= 1ull << 63;
    assert(vmm_core_translate(&state, (1ull << 30) + 321, &mapping) && mapping.flags == VMM_WRITE);
    copy[0] &= ~(1ull << 63);
    uint64_t base = state.stats.window_base, data = allocate(0), physical;
    unsigned int before = count();
    for (int limit = 0; limit < 3; ++limit) {
        budget = limit;
        assert(vmm_core_map(&state, base, data, VMM_WRITE) == VMM_NO_MEMORY);
        assert(count() == before && state.stats.table_pages == 1 && state.stats.mapped_pages == 0);
        assert(!vmm_core_translate(&state, base, &mapping));
    }
    budget = -1;
    assert(vmm_core_map(&state, base, data, VMM_WRITE) == VMM_OK);
    assert(vmm_core_translate(&state, base + 57, &mapping) && mapping.physical == data + 57);
    assert(mapping.page_size == 4096 && mapping.flags == VMM_WRITE);
    assert(state.stats.table_pages == 4 && state.stats.mapped_pages == 1);
    before = count();
    budget = 1;
    assert(vmm_core_map(&state, base + (1ull << 30), data, VMM_WRITE) == VMM_NO_MEMORY);
    assert(count() == before && state.stats.table_pages == 4 && vmm_core_translate(&state, base, &mapping));
    budget = -1;
    assert(vmm_core_map(&state, base, data, 0) == VMM_ALREADY_MAPPED && count() == before);
    assert(vmm_core_alloc(&state, base, 0, &physical) == VMM_ALREADY_MAPPED && physical == 0 && count() == before);
    assert(vmm_core_map(&state, base + 1, data, 0) == VMM_BAD_ADDRESS);
    assert(vmm_core_map(&state, 0x800000000000ull, data, 0) == VMM_BAD_ADDRESS);
    assert(vmm_core_map(&state, 0xffffffff80000000ull, data, 0) == VMM_PROTECTED);
    assert(vmm_core_map(&state, base + 4096, data + 1, 0) == VMM_BAD_PHYSICAL);
    assert(vmm_core_map(&state, base + 4096, 0, 0) == VMM_BAD_PHYSICAL);
    assert(vmm_core_map(&state, base + 4096, 1ull << 48, 0) == VMM_BAD_PHYSICAL);
    assert(vmm_core_map(&state, base + 4096, data, 3) == VMM_BAD_FLAGS);
    assert(vmm_core_map(&state, base + 4096, data, 4) == VMM_BAD_FLAGS);
    assert(vmm_core_map(&state, base + 4096, data, 0) == VMM_OK);
    assert(vmm_core_protect(&state, base, VMM_EXEC) == VMM_OK);
    assert(vmm_core_translate(&state, base, &mapping) && mapping.flags == VMM_EXEC);
    assert(vmm_core_unmap(&state, base, &physical) == VMM_OK && physical == data);
    assert(allocated(0, data) && state.stats.table_pages == 4);
    assert(vmm_core_translate(&state, base + 4096, &mapping));
    assert(vmm_core_unmap(&state, base + 4096, 0) == VMM_OK && state.stats.table_pages == 1);
    assert(vmm_core_unmap(&state, base + 4096, 0) == VMM_NOT_MAPPED);
    assert(vmm_core_protect(&state, base, 0) == VMM_NOT_MAPPED);
    release(0, data);
    data = allocate(0);
    assert(vmm_core_map(&state, base, data, VMM_WRITE) == VMM_OK);
    assert(vmm_core_map(&state, base + (1ull << 21), data, VMM_WRITE) == VMM_OK);
    assert(vmm_core_map(&state, base + (1ull << 30), data, VMM_WRITE) == VMM_OK);
    assert(state.stats.table_pages == 7 && state.stats.mapped_pages == 3);
    assert(vmm_core_unmap(&state, base, 0) == VMM_OK && state.stats.table_pages == 6);
    assert(vmm_core_translate(&state, base + (1ull << 21), &mapping) && mapping.physical == data);
    assert(vmm_core_unmap(&state, base + (1ull << 21), 0) == VMM_OK && state.stats.table_pages == 4);
    assert(vmm_core_translate(&state, base + (1ull << 30), &mapping) && mapping.physical == data);
    assert(vmm_core_unmap(&state, base + (1ull << 30), 0) == VMM_OK && state.stats.table_pages == 1);
    release(0, data);
    assert(vmm_core_alloc(&state, base + (1ull << 30), 0, &data) == VMM_OK);
    assert(vmm_core_unmap(&state, base + (1ull << 30), &physical) == VMM_OK && physical == data);
    release(0, data);
    assert(count() == 4 && state.stats.mapped_pages == 0 && state.stats.table_pages == 1);
    assert(memcmp(frames[1], inherited, sizeof inherited) == 0 && used[3] && used[4]);
    assert(flushes && invalidations);
    puts("OK: VMM, heranca, huge pages, aliases, permissoes, OOM transacional e limpeza.");
    return 0;
}
