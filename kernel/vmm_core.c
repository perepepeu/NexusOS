#include <vmm_internal.h>

#define PRESENT 1ull
#define WRITABLE 2ull
#define HUGE 128ull
#define OWN_TABLE (1ull << 10)
#define NX (1ull << 63)

static bool canonical(uint64_t address) {
    return (address >> 48) == ((address & (1ull << 47)) ? 0xffff : 0);
}
static enum vmm_result validate(struct vmm_state *s, uint64_t address) {
    if (!s || !s->stats.ready) return VMM_NOT_READY;
    if (!canonical(address) || address % 4096) return VMM_BAD_ADDRESS;
    return ((address >> 39) & 511) == s->slot ? VMM_OK : VMM_PROTECTED;
}
static bool valid_flags(unsigned int flags) {
    return !(flags & ~(VMM_WRITE | VMM_EXEC)) && flags != (VMM_WRITE | VMM_EXEC);
}
static uint64_t leaf_flags(unsigned int flags) {
    return PRESENT | ((flags & VMM_WRITE) ? WRITABLE : 0) | ((flags & VMM_EXEC) ? 0 : NX);
}

enum vmm_result vmm_core_init(struct vmm_state *s, const struct vmm_ops *ops,
        void *context, uint64_t inherited, unsigned int bits) {
    if (!s || !ops || !ops->allocate || !ops->release || !ops->table || !ops->allocated ||
            !ops->invalidate || !ops->flush) return VMM_BAD_ARGUMENT;
    if (s->stats.ready) return VMM_ALREADY_INITIALIZED;
    if (bits < 32 || bits > 52 || !inherited || inherited % 4096) return VMM_UNSUPPORTED;
    uint64_t mask = ((1ull << bits) - 1) & ~4095ull;
    if (inherited & ~mask) return VMM_BAD_PHYSICAL;
    uint64_t *old = ops->table(context, inherited);
    if (!old) return VMM_BAD_PHYSICAL;
    unsigned int slot;
    for (slot = 384; slot < 508 && old[slot]; ++slot) {}
    if (slot == 508) return VMM_PROTECTED;
    uint64_t root = ops->allocate(context);
    if (!root) return VMM_NO_MEMORY;
    uint64_t *copy = ops->table(context, root);
    if (!copy || (root & ~mask)) { ops->release(context, root); return VMM_BAD_PHYSICAL; }
    for (unsigned int i = 0; i < 512; ++i) copy[i] = old[i];
    s->ops = *ops; s->context = context; s->physical_mask = mask; s->slot = slot;
    s->stats = (struct vmm_stats){.root_address = root,
        .window_base = 0xffff000000000000ull | ((uint64_t)slot << 39),
        .window_size = 1ull << 39, .table_pages = 1, .ready = true};
    return VMM_OK;
}

static void rollback(struct vmm_state *s, uint64_t **parents, uint64_t *pages, unsigned int count) {
    if (!count) return;
    for (unsigned int i = count; i; --i) *parents[i - 1] = 0;
    s->ops.flush(s->context);
    for (unsigned int i = count; i; --i) {
        s->ops.release(s->context, pages[i - 1]); --s->stats.table_pages;
    }
}

enum vmm_result vmm_core_map(struct vmm_state *s, uint64_t va, uint64_t physical, unsigned int flags) {
    enum vmm_result result = validate(s, va);
    if (result != VMM_OK) return result;
    if (!valid_flags(flags)) return VMM_BAD_FLAGS;
    if (!physical || (physical & ~s->physical_mask) || !s->ops.allocated(s->context, physical))
        return VMM_BAD_PHYSICAL;
    uint64_t *table = s->ops.table(s->context, s->stats.root_address);
    uint64_t *created_parents[3], created_pages[3];
    unsigned int created = 0;
    for (unsigned int level = 4; level > 1; --level) {
        if (!table) { result = VMM_BAD_PHYSICAL; goto fail; }
        uint64_t *entry = &table[(va >> (12 + 9 * (level - 1))) & 511];
        if (!(*entry & PRESENT)) {
            uint64_t page = s->ops.allocate(s->context);
            if (!page) { result = VMM_NO_MEMORY; goto fail; }
            if ((page & ~s->physical_mask) || !s->ops.table(s->context, page)) {
                s->ops.release(s->context, page); result = VMM_BAD_PHYSICAL; goto fail;
            }
            *entry = page | PRESENT | WRITABLE | OWN_TABLE;
            created_parents[created] = entry; created_pages[created++] = page;
            ++s->stats.table_pages;
        } else if ((*entry & HUGE) || !(*entry & OWN_TABLE)) { result = VMM_PROTECTED; goto fail; }
        table = s->ops.table(s->context, *entry & s->physical_mask);
    }
    if (!table) { result = VMM_BAD_PHYSICAL; goto fail; }
    uint64_t *leaf = &table[(va >> 12) & 511];
    if (*leaf & PRESENT) { result = VMM_ALREADY_MAPPED; goto fail; }
    *leaf = physical | leaf_flags(flags);
    ++s->stats.mapped_pages;
    s->ops.invalidate(s->context, va);
    return VMM_OK;
fail:
    rollback(s, created_parents, created_pages, created);
    return result;
}

enum vmm_result vmm_core_alloc(struct vmm_state *s, uint64_t va, unsigned int flags, uint64_t *physical) {
    if (!physical) return VMM_BAD_ARGUMENT;
    *physical = 0;
    enum vmm_result result = validate(s, va);
    if (result != VMM_OK) return result;
    if (!valid_flags(flags)) return VMM_BAD_FLAGS;
    uint64_t page = s->ops.allocate(s->context);
    if (!page) return VMM_NO_MEMORY;
    result = vmm_core_map(s, va, page, flags);
    if (result != VMM_OK) s->ops.release(s->context, page);
    else *physical = page;
    return result;
}

static enum vmm_result path(struct vmm_state *s, uint64_t va, uint64_t **tables, uint64_t **parents) {
    enum vmm_result result = validate(s, va);
    if (result != VMM_OK) return result;
    tables[0] = s->ops.table(s->context, s->stats.root_address);
    for (unsigned int i = 0; i < 3; ++i) {
        if (!tables[i]) return VMM_BAD_PHYSICAL;
        parents[i] = &tables[i][(va >> (39 - 9 * i)) & 511];
        if (!(*parents[i] & PRESENT)) return VMM_NOT_MAPPED;
        if ((*parents[i] & HUGE) || !(*parents[i] & OWN_TABLE)) return VMM_PROTECTED;
        tables[i + 1] = s->ops.table(s->context, *parents[i] & s->physical_mask);
    }
    if (!tables[3]) return VMM_BAD_PHYSICAL;
    return tables[3][(va >> 12) & 511] & PRESENT ? VMM_OK : VMM_NOT_MAPPED;
}

static bool empty(const uint64_t *table) {
    for (unsigned int i = 0; i < 512; ++i) if (table[i]) return false;
    return true;
}

enum vmm_result vmm_core_unmap(struct vmm_state *s, uint64_t va, uint64_t *physical) {
    if (physical) *physical = 0;
    uint64_t *tables[4], *parents[3];
    enum vmm_result result = path(s, va, tables, parents);
    if (result != VMM_OK) return result;
    uint64_t *leaf = &tables[3][(va >> 12) & 511];
    uint64_t data = *leaf & s->physical_mask;
    *leaf = 0; --s->stats.mapped_pages;
    s->ops.invalidate(s->context, va);
    uint64_t released[3]; unsigned int count = 0;
    for (unsigned int level = 3; level; --level) {
        if (!empty(tables[level])) break;
        released[count++] = *parents[level - 1] & s->physical_mask;
        *parents[level - 1] = 0;
    }
    if (count) s->ops.flush(s->context);
    for (unsigned int i = 0; i < count; ++i) {
        s->ops.release(s->context, released[i]); --s->stats.table_pages;
    }
    if (physical) *physical = data;
    return VMM_OK;
}

enum vmm_result vmm_core_protect(struct vmm_state *s, uint64_t va, unsigned int flags) {
    if (!valid_flags(flags)) return VMM_BAD_FLAGS;
    uint64_t *tables[4], *parents[3];
    enum vmm_result result = path(s, va, tables, parents);
    if (result != VMM_OK) return result;
    uint64_t *leaf = &tables[3][(va >> 12) & 511];
    *leaf = (*leaf & s->physical_mask) | leaf_flags(flags);
    s->ops.invalidate(s->context, va);
    return VMM_OK;
}

bool vmm_core_translate(struct vmm_state *s, uint64_t va, struct vmm_mapping *out) {
    if (!s || !s->stats.ready || !out || !canonical(va)) return false;
    uint64_t physical = s->stats.root_address;
    bool writable = true, executable = true;
    for (unsigned int level = 4; level; --level) {
        uint64_t *table = s->ops.table(s->context, physical);
        if (!table) return false;
        uint64_t entry = table[(va >> (12 + 9 * (level - 1))) & 511];
        if (!(entry & PRESENT)) return false;
        writable = writable && (entry & WRITABLE);
        executable = executable && !(entry & NX);
        if (level == 1 || ((level == 3 || level == 2) && (entry & HUGE))) {
            uint64_t size = 1ull << (12 + 9 * (level - 1));
            out->physical = (entry & s->physical_mask & ~(size - 1)) | (va & (size - 1));
            out->page_size = size;
            out->flags = (writable ? VMM_WRITE : 0) | (executable ? VMM_EXEC : 0);
            return true;
        }
        physical = entry & s->physical_mask;
    }
    return false;
}
