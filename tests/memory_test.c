#include <assert.h>
#include <stdio.h>
#include <memory.h>

static struct limine_memmap_response response;
static struct limine_memmap_entry *pointers[MEMORY_MAX_REGIONS];

static enum memory_error detect(struct limine_memmap_entry *entries, unsigned int count) {
    for (unsigned int i = 0; i < count; ++i) pointers[i] = &entries[i];
    response = (struct limine_memmap_response){.entry_count = count, .entries = pointers};
    return memory_detect(&response);
}

int main(void) {
    struct limine_memmap_entry mixed[] = {
        {0, 0x3000, LIMINE_MEMMAP_USABLE},
        {0x3000, 0x2000, LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE},
        {0x5000, 0x3000, LIMINE_MEMMAP_RESERVED},
        {0x6000, 0x3000, LIMINE_MEMMAP_RESERVED_MAPPED},
        {0x7000, 0x1000, LIMINE_MEMMAP_ACPI_NVS},
        {0x100000000ull, 0x200000000ull, LIMINE_MEMMAP_USABLE},
    };
    assert(detect(mixed, 6) == MEMORY_OK);
    const struct memory_stats *stats = memory_get_stats();
    assert(stats->bytes[MEMORY_USABLE] == 0x200003000ull);
    assert(stats->bytes[MEMORY_RESERVED] == 0x4000); /* Uniao, sem duplicar a sobreposicao. */
    assert(stats->bytes[MEMORY_BOOTLOADER] == 0x2000 && stats->bytes[MEMORY_NVS] == 0x1000);
    assert(stats->usable_pages == 0x200002 && stats->largest_base == 0x100000000ull);
    assert(stats->largest_bytes == 0x200000000ull && stats->highest_address == 0x300000000ull);
    mixed[0].length = 1; /* A copia propria nao muda com o buffer do bootloader. */
    assert(memory_get_region(0)->length == 0x3000);
    assert(!memory_get_region(6));

    struct limine_memmap_entry adjacent[] = {{1, 4095, 0}, {4096, 8192, 0}};
    assert(detect(adjacent, 2) == MEMORY_OK);
    assert(memory_get_stats()->usable_pages == 2 && memory_get_stats()->largest_bytes == 8192);
    struct limine_memmap_entry unknown[] = {{0x1000, 0x1000, 999}, {0x2000, 0x1000, LIMINE_MEMMAP_BAD_MEMORY}};
    assert(detect(unknown, 2) == MEMORY_OK);
    assert(memory_get_stats()->usable_pages == 0 && memory_get_stats()->bytes[MEMORY_UNKNOWN] == 4096);
    assert(memory_get_stats()->bytes[MEMORY_BAD] == 4096);
    struct limine_memmap_entry overlap[] = {{0x1000, 0x3000, 1}, {0x2000, 4096, 0}};
    assert(detect(overlap, 2) == MEMORY_OVERLAP);
    overlap[0].type = 0; overlap[1].type = 1;
    assert(detect(overlap, 2) == MEMORY_OVERLAP);
    overlap[0].type = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
    assert(detect(overlap, 2) == MEMORY_OVERLAP);
    struct limine_memmap_entry unsorted[] = {{0x3000, 4096, 1}, {0x1000, 4096, 1}};
    assert(detect(unsorted, 2) == MEMORY_UNSORTED);
    struct limine_memmap_entry overflow[] = {{UINT64_MAX - 1, 3, 1}};
    assert(detect(overflow, 1) == MEMORY_OVERFLOW);
    overflow[0].length = 0;
    assert(detect(overflow, 1) == MEMORY_BAD_ENTRY);
    assert(memory_detect(0) == MEMORY_MISSING);
    response.entry_count = MEMORY_MAX_REGIONS + 1;
    assert(memory_detect(&response) == MEMORY_TOO_MANY);
    response.entry_count = 1; pointers[0] = 0;
    assert(memory_detect(&response) == MEMORY_BAD_ENTRY);
    assert(!memory_get_stats()->valid && !memory_get_region(0));
    puts("OK: mapa fisico, 64 bits, paginas, sobreposicoes, tipos e entradas invalidas.");
    return 0;
}
