#include <resource_internal.h>
int resource_reserve(struct resource_registry *r, uint64_t owner) {
    if (!r || r->draining || !owner) return -1;
    for (unsigned int i = 0; i < RESOURCE_MAX; ++i) if (r->entries[i].kind == RESOURCE_EMPTY) {
        r->entries[i] = (struct resource_entry){.kind = RESOURCE_RESERVED, .owner = owner}; return (int)i;
    }
    return -1;
}
int resource_find(struct resource_registry *r, uintptr_t address) {
    if (!r || !address) return -1;
    for (unsigned int i = 0; i < RESOURCE_MAX; ++i)
        if (r->entries[i].kind > RESOURCE_RESERVED && r->entries[i].address == address) return (int)i;
    return -1;
}
bool resource_commit(struct resource_registry *r, int token, uintptr_t address, enum resource_kind kind,
        size_t count, void (*release)(void *)) {
    if (!r || r->draining || token < 0 || token >= RESOURCE_MAX || !address || !count ||
            kind < RESOURCE_HEAP || kind > RESOURCE_CUSTOM || (kind == RESOURCE_CUSTOM && !release)) return false;
    struct resource_entry *e = &r->entries[token];
    if (e->kind != RESOURCE_RESERVED && !(e->kind == RESOURCE_HEAP && kind == RESOURCE_HEAP)) return false;
    int duplicate = resource_find(r, address);
    if (duplicate >= 0 && duplicate != token) return false;
    *e = (struct resource_entry){.owner = e->owner, .address = address, .count = count, .kind = kind, .release = release};
    return true;
}
void resource_cancel(struct resource_registry *r, int token) {
    if (r && token >= 0 && token < RESOURCE_MAX && r->entries[token].kind == RESOURCE_RESERVED)
        r->entries[token] = (struct resource_entry){0};
}
size_t resource_count(struct resource_registry *r, uint64_t owner) {
    size_t count = 0;
    if (r) for (unsigned int i = 0; i < RESOURCE_MAX; ++i)
        if (r->entries[i].kind != RESOURCE_EMPTY && r->entries[i].owner == owner) ++count;
    return count;
}
bool resource_release(struct resource_registry *r, int token, void (*dispose)(const struct resource_entry *)) {
    if (!r || r->draining || token < 0 || token >= RESOURCE_MAX || r->entries[token].kind <= RESOURCE_RESERVED || !dispose) return false;
    struct resource_entry entry = r->entries[token];
    r->entries[token] = (struct resource_entry){0};
    r->draining = true; dispose(&entry); r->draining = false; return true;
}
size_t resource_reap(struct resource_registry *r, uint64_t owner, void (*dispose)(const struct resource_entry *)) {
    if (!r || r->draining || !dispose) return 0;
    size_t count = 0;
    r->draining = true;
    for (unsigned int i = 0; i < RESOURCE_MAX; ++i) if (r->entries[i].kind && r->entries[i].owner == owner) {
        struct resource_entry entry = r->entries[i]; r->entries[i] = (struct resource_entry){0};
        if (entry.kind > RESOURCE_RESERVED) { dispose(&entry); ++count; }
    }
    r->draining = false; return count;
}
