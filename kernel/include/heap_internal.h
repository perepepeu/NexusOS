#ifndef NEXUS_HEAP_INTERNAL_H
#define NEXUS_HEAP_INTERNAL_H
#include <heap.h>
struct heap_arena;
struct heap_ops { void *(*acquire)(size_t bytes); void (*release)(void *base, size_t bytes); };
enum heap_error { HEAP_OK, HEAP_INVALID_POINTER, HEAP_CORRUPTION, HEAP_FREED_WRITE };
struct heap_state { struct heap_arena *first; struct heap_ops ops; struct heap_stats stats;
    enum heap_error error; uintptr_t error_address; };
bool heap_core_init(struct heap_state *s, struct heap_ops ops);
void *heap_core_alloc(struct heap_state *s, size_t size);
void *heap_core_calloc(struct heap_state *s, size_t count, size_t size);
void *heap_core_realloc(struct heap_state *s, void *pointer, size_t size);
bool heap_core_free(struct heap_state *s, void *pointer);
enum heap_error heap_core_check(struct heap_state *s);
bool heap_core_validate_pointer(struct heap_state *s, void *pointer);
#endif
