#ifndef NEXUS_MEMORY_H
#define NEXUS_MEMORY_H
#include <stdbool.h>
#include <stdint.h>
#include <limine.h>

enum { MEMORY_MAX_REGIONS = 1024, MEMORY_PAGE_SIZE = 4096 };
enum memory_class {
    MEMORY_USABLE, MEMORY_BOOTLOADER, MEMORY_ACPI, MEMORY_NVS, MEMORY_KERNEL,
    MEMORY_RESERVED, MEMORY_BAD, MEMORY_FRAMEBUFFER, MEMORY_UNKNOWN, MEMORY_CLASS_COUNT
};
enum memory_error {
    MEMORY_OK, MEMORY_MISSING, MEMORY_TOO_MANY, MEMORY_BAD_ENTRY,
    MEMORY_UNSORTED, MEMORY_OVERLAP, MEMORY_OVERFLOW
};
struct memory_stats {
    uint64_t bytes[MEMORY_CLASS_COUNT];
    uint64_t ram_bytes; /* Uniao das regioes identificadas como RAM; exclui reservada/MMIO. */
    uint64_t usable_pages, largest_base, largest_bytes, highest_address;
    unsigned int region_count;
    bool valid;
};

enum memory_error memory_detect(const struct limine_memmap_response *map);
const struct memory_stats *memory_get_stats(void);
const struct limine_memmap_entry *memory_get_region(unsigned int index);
const char *memory_error_name(enum memory_error error);
const char *memory_type_name(uint64_t type);
bool memory_test_run(void);
#endif
