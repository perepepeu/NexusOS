#ifndef NEXUS_VMM_H
#define NEXUS_VMM_H
#include <stdbool.h>
#include <stdint.h>

enum vmm_flags { VMM_WRITE = 1, VMM_EXEC = 2 };
enum vmm_result {
    VMM_OK, VMM_NOT_READY, VMM_BAD_ARGUMENT, VMM_BAD_ADDRESS, VMM_BAD_PHYSICAL,
    VMM_BAD_FLAGS, VMM_PROTECTED, VMM_ALREADY_MAPPED, VMM_NOT_MAPPED,
    VMM_NO_MEMORY, VMM_UNSUPPORTED, VMM_ALREADY_INITIALIZED
};
struct vmm_stats {
    uint64_t root_address, window_base, window_size, mapped_pages, table_pages;
    bool ready;
};
struct vmm_mapping { uint64_t physical, page_size; unsigned int flags; };
enum vmm_result vmm_init(uint64_t hhdm);
enum vmm_result vmm_map_page(uint64_t virtual_address, uint64_t physical, unsigned int flags);
enum vmm_result vmm_alloc_page(uint64_t virtual_address, unsigned int flags, uint64_t *physical);
/* Desmapear nunca libera a pagina de dados. O chamador gerencia seus aliases. */
enum vmm_result vmm_unmap_page(uint64_t virtual_address, uint64_t *physical);
enum vmm_result vmm_protect_page(uint64_t virtual_address, unsigned int flags);
bool vmm_translate(uint64_t virtual_address, struct vmm_mapping *mapping);
void vmm_get_stats(struct vmm_stats *stats);
void vmm_selftest(uint64_t hhdm, unsigned int fault);
#endif
