#ifndef NEXUS_RESOURCE_INTERNAL_H
#define NEXUS_RESOURCE_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { RESOURCE_MAX = 256 };
enum resource_kind { RESOURCE_EMPTY, RESOURCE_RESERVED, RESOURCE_HEAP, RESOURCE_PAGES, RESOURCE_MAPPING, RESOURCE_CUSTOM };
struct resource_entry { uint64_t owner; uintptr_t address; size_t count; enum resource_kind kind; void (*release)(void *); };
struct resource_registry { struct resource_entry entries[RESOURCE_MAX]; bool draining; };
int resource_reserve(struct resource_registry *r, uint64_t owner);
int resource_find(struct resource_registry *r, uintptr_t address);
bool resource_commit(struct resource_registry *r, int token, uintptr_t address, enum resource_kind kind, size_t count, void (*release)(void *));
void resource_cancel(struct resource_registry *r, int token);
size_t resource_count(struct resource_registry *r, uint64_t owner);
bool resource_release(struct resource_registry *r, int token, void (*dispose)(const struct resource_entry *));
size_t resource_reap(struct resource_registry *r, uint64_t owner, void (*dispose)(const struct resource_entry *));
#endif
