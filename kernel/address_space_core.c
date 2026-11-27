#include <address_space.h>
#define PRESENT 1ull
#define WRITE 2ull
#define USER 4ull
#define HUGE 128ull
#define NX (1ull << 63)

static bool flags_valid(unsigned int flags) {
    return !(flags & ~(VMM_WRITE | VMM_EXEC)) && flags != (VMM_WRITE | VMM_EXEC);
}
bool address_core_create(struct address_space *s, const struct vmm_ops *ops, void *context, uint64_t kernel, uint64_t mask) {
    if (!s || s->ready || !ops || !ops->allocate || !ops->release || !ops->table || !ops->flush ||
            !ops->invalidate || !kernel || (kernel & ~mask)) return false;
    uint64_t *source = ops->table(context, kernel);
    if (!source) return false;
    uint64_t root = ops->allocate(context);
    if (!root) return false;
    uint64_t *target = ops->table(context, root);
    if ((root & ~mask) || !target) { ops->release(context, root); return false; }
    /* Nao herda mapas baixos/identidade. Kernel/HHDM/IRQ ficam supervisor em
     * todos os espacos; os descendentes da metade alta sao compartilhados. */
    for (unsigned int i = 0; i < 512; ++i) target[i] = i < 256 ? 0 : source[i] & ~USER;
    *s = (struct address_space){.ops = *ops, .context = context, .root = root,
        .physical_mask = mask, .tables = 1, .ready = true};
    return true;
}
uint64_t address_core_alloc(struct address_space *s, uint64_t va, unsigned int flags) {
    if (!s || !s->ready || va < 4096 || va >= USER_ADDRESS_END || (va & 4095) ||
            !flags_valid(flags) || s->pages >= USER_SPACE_MAX_PAGES) return 0;
    uint64_t *table = s->ops.table(s->context, s->root);
    uint64_t *parents[3], created[3]; unsigned int count = 0;
    uint64_t data = s->ops.allocate(s->context);
    if (!data) return 0;
    if ((data & ~s->physical_mask) || !s->ops.table(s->context, data)) goto rollback;
    for (unsigned int level = 4; level > 1; --level) {
        if (!table) goto rollback;
        uint64_t *entry = &table[(va >> (12 + 9 * (level - 1))) & 511];
        if (!(*entry & PRESENT)) {
            uint64_t next = s->ops.allocate(s->context);
            if (!next) goto rollback;
            if ((next & ~s->physical_mask) || !s->ops.table(s->context, next)) {
                s->ops.release(s->context, next); goto rollback;
            }
            *entry = next | PRESENT | WRITE | USER;
            parents[count] = entry; created[count++] = next; ++s->tables;
        } else if ((*entry & HUGE) || !(*entry & USER)) goto rollback;
        table = s->ops.table(s->context, *entry & s->physical_mask);
    }
    if (!table || (table[(va >> 12) & 511] & PRESENT)) goto rollback;
    table[(va >> 12) & 511] = data | PRESENT | USER | ((flags & VMM_WRITE) ? WRITE : 0) | ((flags & VMM_EXEC) ? 0 : NX);
    ++s->pages; s->ops.invalidate(s->context, va); return data;
rollback:
    for (unsigned int i = count; i; --i) *parents[i - 1] = 0;
    if (count) s->ops.flush(s->context);
    for (unsigned int i = count; i; --i) { s->ops.release(s->context, created[i - 1]); --s->tables; }
    s->ops.release(s->context, data); return 0;
}
bool address_core_translate(struct address_space *s, uint64_t va, struct vmm_mapping *out) {
    if (!s || !s->ready || !out || (va >> 48) != ((va & (1ull << 47)) ? 0xffff : 0)) return false;
    uint64_t physical = s->root; bool write = true, execute = true, user = true;
    for (unsigned int level = 4; level; --level) {
        uint64_t *table = s->ops.table(s->context, physical);
        if (!table) return false;
        uint64_t entry = table[(va >> (12 + 9 * (level - 1))) & 511];
        if (!(entry & PRESENT)) return false;
        write = write && (entry & WRITE); execute = execute && !(entry & NX); user = user && (entry & USER);
        if (level == 1 || ((level == 3 || level == 2) && (entry & HUGE))) {
            uint64_t size = 1ull << (12 + 9 * (level - 1));
            *out = (struct vmm_mapping){.physical = (entry & s->physical_mask & ~(size - 1)) | (va & (size - 1)),
                .page_size = size, .flags = (write ? VMM_WRITE : 0) | (execute ? VMM_EXEC : 0) | (user ? VMM_USER : 0)};
            return true;
        }
        physical = entry & s->physical_mask;
    }
    return false;
}
bool address_core_copy(struct address_space *s, uint64_t va, void *buffer, size_t bytes, bool to_user) {
    if (!s || !s->ready || va >= USER_ADDRESS_END || bytes > USER_ADDRESS_END - va || (bytes && !buffer)) return false;
    unsigned char *data = buffer;
    while (bytes) {
        struct vmm_mapping mapping;
        if (!address_core_translate(s, va, &mapping) || !(mapping.flags & VMM_USER) || mapping.page_size != 4096) return false;
        unsigned char *page = (unsigned char *)s->ops.table(s->context, mapping.physical & ~4095ull);
        if (!page) return false;
        size_t offset = va & 4095, count = 4096 - offset;
        if (count > bytes) count = bytes;
        for (size_t i = 0; i < count; ++i) {
            if (to_user) page[offset + i] = data[i]; else data[i] = page[offset + i];
        }
        data += count; va += count; bytes -= count;
    }
    return true;
}
static void destroy_table(struct address_space *s, uint64_t physical, unsigned int level) {
    uint64_t *table = s->ops.table(s->context, physical);
    for (unsigned int i = 0; i < 512; ++i) if (table[i] & PRESENT) {
        uint64_t next = table[i] & s->physical_mask;
        if (level == 1) { s->ops.release(s->context, next); --s->pages; }
        else destroy_table(s, next, level - 1);
        table[i] = 0;
    }
    s->ops.release(s->context, physical); --s->tables;
}
bool address_core_destroy(struct address_space *s, uint64_t active_root) {
    if (!s || !s->ready || s->root == active_root) return false;
    uint64_t *root = s->ops.table(s->context, s->root);
    for (unsigned int i = 0; i < 256; ++i) if (root[i] & PRESENT) {
        destroy_table(s, root[i] & s->physical_mask, 3); root[i] = 0;
    }
    s->ops.release(s->context, s->root);
    *s = (struct address_space){0}; return true;
}
