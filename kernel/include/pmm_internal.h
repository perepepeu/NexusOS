#ifndef NEXUS_PMM_INTERNAL_H
#define NEXUS_PMM_INTERNAL_H
#include <memory.h>
#include <pmm.h>

struct pmm_range { uint64_t first_page, pages, first_bit; };
struct pmm_state {
    struct pmm_range ranges[MEMORY_MAX_REGIONS];
    struct pmm_stats stats;
    uint8_t *bitmap;
    uint64_t hint;
};
/* Nucleo sem instrucoes privilegiadas; o chamador serializa os acessos. */
enum pmm_result pmm_core_prepare(struct pmm_state *state);
enum pmm_result pmm_core_activate(struct pmm_state *state, void *bitmap, uint64_t capacity);
uint64_t pmm_core_alloc(struct pmm_state *state, uint64_t count, uint64_t alignment);
enum pmm_result pmm_core_free(struct pmm_state *state, uint64_t address, uint64_t count);
#endif
