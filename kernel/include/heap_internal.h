#ifndef NEXUS_HEAP_INTERNAL_H
#define NEXUS_HEAP_INTERNAL_H
#include <heap.h>
struct heap_arena;
struct heap_ops { void *(*acquire)(size_t bytes); void (*release)(void *base, size_t bytes); };
struct heap_state { struct heap_arena *first; struct heap_ops ops; struct heap_stats stats; };
bool heap_core_init(struct heap_state *s, struct heap_ops ops);
void *heap_core_alloc(struct heap_state *s, size_t size);
void *heap_core_calloc(struct heap_state *s, size_t count, size_t size);
void *heap_core_realloc(struct heap_state *s, void *pointer, size_t size);
bool heap_core_free(struct heap_state *s, void *pointer);
bool heap_core_contains(struct heap_state *s, void *pointer);
#endif
