#ifndef NEXUS_PMM_H
#define NEXUS_PMM_H
#include <stdbool.h>
#include <stdint.h>

enum pmm_result {
    PMM_OK, PMM_NOT_READY, PMM_BAD_ARGUMENT, PMM_INVALID_ADDRESS,
    PMM_NOT_ALLOCATED, PMM_METADATA, PMM_NO_MEMORY, PMM_MAP_INVALID, PMM_ALREADY_INITIALIZED
};
struct pmm_stats {
    uint64_t total_pages, free_pages, allocated_pages, metadata_pages;
    uint64_t metadata_address, bitmap_bytes;
    unsigned int range_count;
    bool ready;
};
enum pmm_result pmm_init(uint64_t hhdm);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_zeroed_page(void);
uint64_t pmm_alloc_pages(uint64_t count, uint64_t alignment_pages);
enum pmm_result pmm_free_page(uint64_t address);
enum pmm_result pmm_free_pages(uint64_t address, uint64_t count);
void pmm_get_stats(struct pmm_stats *out);
void pmm_selftest(uint64_t hhdm);
#endif
