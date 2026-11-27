#ifndef NEXUS_VMM_INTERNAL_H
#define NEXUS_VMM_INTERNAL_H
#include <vmm.h>

struct vmm_ops {
    uint64_t (*allocate)(void *);
    void (*release)(void *, uint64_t);
    uint64_t *(*table)(void *, uint64_t);
    bool (*allocated)(void *, uint64_t);
    void (*invalidate)(void *, uint64_t);
    void (*flush)(void *);
};
struct vmm_state {
    struct vmm_stats stats;
    struct vmm_ops ops;
    void *context;
    uint64_t physical_mask;
    unsigned int slot;
};
enum vmm_result vmm_core_init(struct vmm_state *, const struct vmm_ops *, void *, uint64_t, unsigned int);
enum vmm_result vmm_core_map(struct vmm_state *, uint64_t, uint64_t, unsigned int);
enum vmm_result vmm_core_alloc(struct vmm_state *, uint64_t, unsigned int, uint64_t *);
enum vmm_result vmm_core_unmap(struct vmm_state *, uint64_t, uint64_t *);
enum vmm_result vmm_core_protect(struct vmm_state *, uint64_t, unsigned int);
bool vmm_core_translate(struct vmm_state *, uint64_t, struct vmm_mapping *);
#endif
