#include <process_resources.h>
#include <resource_internal.h>
#include <process.h>
#include <heap.h>
#include <pmm.h>
#include <vmm.h>
#include <io.h>
#include <panic.h>

static struct resource_registry registry;
enum { OWNED_MAP_PAGES = 8192 };
static unsigned char mapped[OWNED_MAP_PAGES];
static uint64_t mapping_base(void) { struct vmm_stats v; vmm_get_stats(&v); return v.window_base + (3ull << 30); }

int process_heap_prepare(void *pointer) {
    if (registry.draining) return -2;
    int token = resource_find(&registry, (uintptr_t)pointer);
    if (token >= 0) return registry.entries[token].kind == RESOURCE_HEAP ? token : -2;
    uint64_t owner = process_current_pid();
    if (owner <= 1) return -1;
    token = resource_reserve(&registry, owner); return token < 0 ? -2 : token;
}
void process_heap_finish(int token, void *old, void *pointer, size_t size) {
    (void)old;
    if (token < 0) return;
    if (!pointer) { resource_cancel(&registry, token); return; }
    KASSERT(resource_commit(&registry, token, (uintptr_t)pointer, RESOURCE_HEAP, size, 0));
}
void process_heap_forget(void *pointer) {
    int token = resource_find(&registry, (uintptr_t)pointer);
    if (token >= 0 && registry.entries[token].kind == RESOURCE_HEAP) registry.entries[token] = (struct resource_entry){0};
}
static void dispose(const struct resource_entry *e) {
    if (e->kind == RESOURCE_HEAP) kfree((void *)e->address);
    else if (e->kind == RESOURCE_PAGES) KASSERT(pmm_free_pages(e->address, e->count) == PMM_OK);
    else if (e->kind == RESOURCE_MAPPING) {
        uint64_t base = mapping_base();
        for (size_t i = 0; i < e->count; ++i) {
            uint64_t physical;
            KASSERT(vmm_unmap_page(e->address + i * 4096, &physical) == VMM_OK);
            KASSERT(pmm_free_page(physical) == PMM_OK);
            mapped[(e->address - base) / 4096 + i] = 0;
        }
    } else if (e->kind == RESOURCE_CUSTOM) e->release((void *)e->address);
}
void process_resources_reap(uint64_t pid) { resource_reap(&registry, pid, dispose); }
size_t process_resource_count(uint64_t owner) {
    uint64_t flags = irq_save(); size_t count = resource_count(&registry, owner); irq_restore(flags); return count;
}
uint64_t process_alloc_pages(uint64_t count, uint64_t alignment) {
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    uint64_t owner = process_current_pid();
    int token = process_owner_alive(owner) ? resource_reserve(&registry, owner) : -1;
    uint64_t physical = token >= 0 ? pmm_alloc_pages(count, alignment) : 0;
    if (!physical) resource_cancel(&registry, token);
    else KASSERT(resource_commit(&registry, token, physical, RESOURCE_PAGES, count, 0));
    irq_restore(flags); return physical;
}
void *process_map_pages(size_t count, unsigned int permissions) {
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    uint64_t owner = process_current_pid();
    int token = count && count <= OWNED_MAP_PAGES && process_owner_alive(owner) ? resource_reserve(&registry, owner) : -1;
    if (token < 0) { irq_restore(flags); return 0; }
    uint64_t base = mapping_base(); size_t run = 0;
    for (size_t i = 0; i < OWNED_MAP_PAGES; ++i) {
        run = mapped[i] ? 0 : run + 1;
        if (run < count) continue;
        size_t start = i + 1 - count, n = 0;
        for (; n < count; ++n) {
            uint64_t physical;
            if (vmm_alloc_page(base + (start + n) * 4096, permissions, &physical) != VMM_OK) break;
            mapped[start + n] = 1;
        }
        if (n < count) {
            struct resource_entry partial = {.kind = RESOURCE_MAPPING, .address = base + start * 4096, .count = n};
            dispose(&partial); resource_cancel(&registry, token); irq_restore(flags); return 0;
        }
        uintptr_t address = base + start * 4096;
        KASSERT(resource_commit(&registry, token, address, RESOURCE_MAPPING, count, 0));
        irq_restore(flags); return (void *)address;
    }
    resource_cancel(&registry, token); irq_restore(flags); return 0;
}
bool process_resource_attach(void *resource, void (*release)(void *)) {
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    uint64_t owner = process_current_pid();
    int token = resource && release && process_owner_alive(owner) ? resource_reserve(&registry, owner) : -1;
    bool ok = resource_commit(&registry, token, (uintptr_t)resource, RESOURCE_CUSTOM, 1, release);
    if (!ok) resource_cancel(&registry, token);
    irq_restore(flags); return ok;
}
bool process_resource_release(void *resource) {
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    int token = resource_find(&registry, (uintptr_t)resource);
    bool ok = token >= 0 && registry.entries[token].owner == process_current_pid() && resource_release(&registry, token, dispose);
    irq_restore(flags); return ok;
}
bool process_resource_transfer(void *resource, uint64_t owner) {
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    int token = resource_find(&registry, (uintptr_t)resource);
    bool ok = !registry.draining && token >= 0 && registry.entries[token].owner == process_current_pid() && process_owner_alive(owner);
    if (ok) registry.entries[token].owner = owner;
    irq_restore(flags); return ok;
}
