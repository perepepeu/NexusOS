#include <memory.h>

static struct limine_memmap_entry regions[MEMORY_MAX_REGIONS];
static struct memory_stats stats;

static unsigned int classify(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE: return MEMORY_USABLE;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return MEMORY_BOOTLOADER;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return MEMORY_ACPI;
        case LIMINE_MEMMAP_ACPI_NVS: return MEMORY_NVS;
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return MEMORY_KERNEL;
        case LIMINE_MEMMAP_RESERVED: case LIMINE_MEMMAP_RESERVED_MAPPED: return MEMORY_RESERVED;
        case LIMINE_MEMMAP_BAD_MEMORY: return MEMORY_BAD;
        case LIMINE_MEMMAP_FRAMEBUFFER: return MEMORY_FRAMEBUFFER;
        default: return MEMORY_UNKNOWN;
    }
}

static bool count_interval(unsigned int category, uint64_t base, uint64_t end) {
    uint64_t bytes = end - base;
    if (stats.bytes[category] > UINT64_MAX - bytes) return false;
    stats.bytes[category] += bytes;
    if (category == MEMORY_USABLE) {
        /* Apenas paginas completas; a pagina fisica zero fica fora da contagem. */
        uint64_t first = base / MEMORY_PAGE_SIZE + (base % MEMORY_PAGE_SIZE != 0);
        uint64_t last = end / MEMORY_PAGE_SIZE;
        if (first == 0) first = 1;
        if (last > first) {
            uint64_t pages = last - first;
            stats.usable_pages += pages;
            if (pages * MEMORY_PAGE_SIZE > stats.largest_bytes) {
                stats.largest_base = first * MEMORY_PAGE_SIZE;
                stats.largest_bytes = pages * MEMORY_PAGE_SIZE;
            }
        }
    }
    return true;
}

enum memory_error memory_detect(const struct limine_memmap_response *map) {
    stats.valid = false;
    stats.region_count = 0;
    stats.ram_bytes = stats.usable_pages = stats.largest_base = stats.largest_bytes = stats.highest_address = 0;
    for (unsigned int i = 0; i < MEMORY_CLASS_COUNT; ++i) stats.bytes[i] = 0;
    if (!map || !map->entries || !map->entry_count) return MEMORY_MISSING;
    if (map->entry_count > MEMORY_MAX_REGIONS) return MEMORY_TOO_MANY;
    uint64_t previous_base = 0, highest = 0, protected_end = 0;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (!entry || !entry->length) return MEMORY_BAD_ENTRY;
        if (entry->base > UINT64_MAX - entry->length) return MEMORY_OVERFLOW;
        if (i && entry->base < previous_base) return MEMORY_UNSORTED;
        bool exclusive = entry->type == LIMINE_MEMMAP_USABLE ||
                         entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
        /* O protocolo permite sobreposicoes entre regioes nao utilizaveis. */
        if ((exclusive && entry->base < highest) || entry->base < protected_end)
            return MEMORY_OVERLAP;
        uint64_t end = entry->base + entry->length;
        if (end > highest) highest = end;
        if (exclusive) protected_end = end;
        previous_base = entry->base;
        regions[i] = *entry;
    }
    uint64_t starts[MEMORY_CLASS_COUNT] = {0}, ends[MEMORY_CLASS_COUNT] = {0};
    bool active[MEMORY_CLASS_COUNT] = {false};
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = &regions[i];
        unsigned int category = classify(entry->type);
        uint64_t end = entry->base + entry->length;
        if (!active[category]) {
            starts[category] = entry->base; ends[category] = end; active[category] = true;
        } else if (entry->base <= ends[category]) {
            if (end > ends[category]) ends[category] = end;
        } else {
            if (!count_interval(category, starts[category], ends[category])) return MEMORY_OVERFLOW;
            starts[category] = entry->base; ends[category] = end;
        }
    }
    for (unsigned int i = 0; i < MEMORY_CLASS_COUNT; ++i)
        if (active[i] && !count_interval(i, starts[i], ends[i])) return MEMORY_OVERFLOW;
    /* Aberturas MMIO nao sao RAM instalada. Uniao entre categorias de RAM
     * evita duplicar entradas nao utilizaveis sobrepostas permitidas pelo Limine. */
    uint64_t ram_end = 0;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = &regions[i];
        unsigned int category = classify(entry->type);
        if (category != MEMORY_USABLE && category != MEMORY_BAD && category != MEMORY_BOOTLOADER && category != MEMORY_ACPI &&
                category != MEMORY_NVS && category != MEMORY_KERNEL) continue;
        uint64_t end = entry->base + entry->length;
        uint64_t start = entry->base > ram_end ? entry->base : ram_end;
        if (end > start) {
            if (stats.ram_bytes > UINT64_MAX - (end - start)) return MEMORY_OVERFLOW;
            stats.ram_bytes += end - start;
        }
        if (end > ram_end) ram_end = end;
    }
    stats.region_count = (unsigned int)map->entry_count;
    stats.highest_address = highest;
    stats.valid = true;
    return MEMORY_OK;
}

const struct memory_stats *memory_get_stats(void) { return &stats; }
const struct limine_memmap_entry *memory_get_region(unsigned int index) {
    return stats.valid && index < stats.region_count ? &regions[index] : 0;
}
const char *memory_error_name(enum memory_error error) {
    static const char *const names[] = {"OK", "Mapa ausente", "Mapa excede 1024 regioes",
        "Entrada nula ou vazia", "Mapa fora de ordem", "Sobreposicao com RAM utilizavel/reclamavel",
        "Overflow no mapa fisico"};
    return (unsigned int)error < sizeof names / sizeof names[0] ? names[error] : "Mapa invalido";
}
const char *memory_type_name(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE: return "USABLE";
        case LIMINE_MEMMAP_RESERVED: return "RESERVED";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return "ACPI_RECLAIMABLE";
        case LIMINE_MEMMAP_ACPI_NVS: return "ACPI_NVS";
        case LIMINE_MEMMAP_BAD_MEMORY: return "BAD_MEMORY";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "BOOTLOADER_RECLAIMABLE";
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "KERNEL_MODULES";
        case LIMINE_MEMMAP_FRAMEBUFFER: return "FRAMEBUFFER";
        case LIMINE_MEMMAP_RESERVED_MAPPED: return "RESERVED_MAPPED";
        default: return "UNKNOWN";
    }
}
