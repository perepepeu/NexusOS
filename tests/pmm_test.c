#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pmm_internal.h>

static struct pmm_state state;
static struct limine_memmap_entry *entries[MEMORY_MAX_REGIONS];
static struct limine_memmap_response map;
static uint8_t *storage;

static void setup(struct limine_memmap_entry *regions, unsigned int count) {
    free(storage); storage = 0;
    state = (struct pmm_state){0};
    for (unsigned int i = 0; i < count; ++i) entries[i] = &regions[i];
    map = (struct limine_memmap_response){.entry_count = count, .entries = entries};
    assert(memory_detect(&map) == MEMORY_OK);
    assert(pmm_core_prepare(&state) == PMM_OK);
    storage = malloc((size_t)state.stats.bitmap_bytes);
    assert(storage);
    storage[0] = 0xa5;
    assert(pmm_core_activate(&state, storage, state.stats.bitmap_bytes - 1) == PMM_BAD_ARGUMENT);
    assert(storage[0] == 0xa5);
    assert(pmm_core_activate(&state, storage, state.stats.bitmap_bytes) == PMM_OK);
}

static void invariant(void) {
    assert(state.stats.total_pages == state.stats.free_pages + state.stats.allocated_pages + state.stats.metadata_pages);
}

int main(void) {
    struct limine_memmap_entry mixed[] = {
        {0, 5 * 4096, LIMINE_MEMMAP_USABLE},
        {5 * 4096, 3 * 4096, LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE},
        {0x8000, 4096, LIMINE_MEMMAP_EXECUTABLE_AND_MODULES},
        {0x9000, 4096, LIMINE_MEMMAP_ACPI_RECLAIMABLE},
        {0xa000, 4096, LIMINE_MEMMAP_ACPI_NVS},
        {0xb000, 4096, LIMINE_MEMMAP_BAD_MEMORY},
        {0xc000, 4096, LIMINE_MEMMAP_FRAMEBUFFER},
        {0xd000, 4096, 999},
        {0x10000, 16 * 4096, LIMINE_MEMMAP_USABLE},
        {0x10000000000ull, 8 * 4096, LIMINE_MEMMAP_USABLE},
    };
    setup(mixed, sizeof mixed / sizeof mixed[0]);
    assert(state.stats.total_pages == 28 && state.stats.bitmap_bytes == 4);
    assert(state.stats.metadata_pages == 1 && state.stats.free_pages == 27);
    assert(pmm_core_prepare(&state) == PMM_ALREADY_INITIALIZED);
    assert(pmm_core_activate(&state, storage, state.stats.bitmap_bytes) == PMM_ALREADY_INITIALIZED);
    uint64_t addresses[28], count = 0;
    while (state.stats.free_pages) {
        uint64_t address = pmm_core_alloc(&state, 1, 1);
        assert(address && address % 4096 == 0 && address != state.stats.metadata_address);
        assert(address < 5 * 4096 || (address >= 0x10000 && address < 0x20000) ||
            (address >= 0x10000000000ull && address < 0x10000008000ull));
        for (uint64_t j = 0; j < count; ++j) assert(addresses[j] != address);
        addresses[count++] = address;
        invariant();
    }
    assert(count == 27 && pmm_core_alloc(&state, 1, 1) == 0);
    assert(pmm_core_free(&state, state.stats.metadata_address, 1) == PMM_METADATA);
    assert(pmm_core_free(&state, 5 * 4096, 1) == PMM_INVALID_ADDRESS);
    for (uint64_t address = 0x5000; address < 0xe000; address += 4096)
        assert(pmm_core_free(&state, address, 1) == PMM_INVALID_ADDRESS);
    assert(pmm_core_free(&state, 1, 1) == PMM_BAD_ARGUMENT);
    assert(pmm_core_free(&state, addresses[0], 0) == PMM_BAD_ARGUMENT);
    assert(pmm_core_free(&state, addresses[0], 1) == PMM_OK);
    assert(pmm_core_free(&state, addresses[0], 1) == PMM_NOT_ALLOCATED);
    assert(pmm_core_alloc(&state, 1, 1) == addresses[0]); /* Reutiliza apos esgotamento/wrap. */
    for (uint64_t i = 0; i < count; ++i) assert(pmm_core_free(&state, addresses[i], 1) == PMM_OK);
    invariant();
    assert(!state.stats.allocated_pages && state.stats.free_pages == 27);

    uint64_t block = pmm_core_alloc(&state, 3, 8);
    assert(block == 0x10000 && (block / 4096) % 8 == 0);
    uint64_t before = state.stats.free_pages;
    assert(pmm_core_free(&state, block, 4) == PMM_NOT_ALLOCATED);
    assert(state.stats.free_pages == before); /* Nenhuma liberacao parcial. */
    assert(pmm_core_free(&state, block, 3) == PMM_OK);
    assert(pmm_core_free(&state, block, UINT64_MAX) == PMM_INVALID_ADDRESS);
    assert(pmm_core_alloc(&state, 0, 1) == 0);
    assert(pmm_core_alloc(&state, 1, 0) == 0 && pmm_core_alloc(&state, 1, 3) == 0);
    assert(pmm_core_alloc(&state, UINT64_MAX, 1) == 0);
    assert(pmm_core_alloc(&state, 1, 1ull << 63) == 0);
    assert(pmm_core_alloc(&state, 17, 1) == 0); /* Total livre suficiente, mas faixas fragmentadas. */
    assert(state.stats.free_pages == 27 && state.stats.allocated_pages == 0);
    invariant();

    struct limine_memmap_entry one[] = {{4096, 4096, LIMINE_MEMMAP_USABLE}};
    setup(one, 1);
    assert(state.stats.free_pages == 0 && !pmm_core_alloc(&state, 1, 1));
    struct limine_memmap_entry adjacent[] = {{1, 4095, 0}, {4096, 8192, 0}};
    setup(adjacent, 2);
    assert(state.stats.total_pages == 2 && state.stats.free_pages == 1);
    assert(pmm_core_alloc(&state, 1, 1) == 8192);

    struct limine_memmap_entry reserved = {4096, 4096, LIMINE_MEMMAP_RESERVED};
    free(storage); storage = 0; state = (struct pmm_state){0};
    entries[0] = &reserved; map.entry_count = 1;
    assert(memory_detect(&map) == MEMORY_OK && pmm_core_prepare(&state) == PMM_NO_MEMORY);
    assert(!pmm_core_alloc(&state, 1, 1));
    assert(pmm_core_free(&state, 4096, 1) == PMM_NOT_READY);
    puts("OK: PMM, esgotamento, reutilizacao, alinhamento, fragmentacao, 64 bits e liberacoes invalidas.");
    return 0;
}
