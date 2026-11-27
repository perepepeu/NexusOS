#include <heap_internal.h>
#include <stdint.h>

struct heap_arena { struct heap_arena *next; size_t bytes; uint64_t reserved[2]; };
struct block { size_t capacity, requested; struct block *prev, *next; struct heap_arena *arena; uint64_t reserved; };
_Static_assert(sizeof(struct heap_arena) % 16 == 0 && sizeof(struct block) % 16 == 0, "heap alignment");

static struct block *first(struct heap_arena *a) { return (struct block *)(a + 1); }
static struct block *find(struct heap_state *s, void *p) {
    if (!s || !s->stats.ready) return 0;
    for (struct heap_arena *a = s->first; a; a = a->next)
        for (struct block *b = first(a); b; b = b->next)
            if ((void *)(b + 1) == p && b->requested) return b;
    return 0;
}
bool heap_core_contains(struct heap_state *s, void *p) { return find(s, p) != 0; }
bool heap_core_init(struct heap_state *s, struct heap_ops ops) {
    if (!s || s->stats.ready || !ops.acquire || !ops.release) return false;
    *s = (struct heap_state){.ops = ops, .stats.ready = true};
    return true;
}
static void split(struct block *b, size_t size) {
    if (b->capacity - size < sizeof(struct block) + 16) return;
    struct block *tail = (struct block *)((unsigned char *)(b + 1) + size);
    *tail = (struct block){.capacity = b->capacity - size - sizeof *tail,
        .prev = b, .next = b->next, .arena = b->arena};
    if (tail->next) tail->next->prev = tail;
    b->next = tail; b->capacity = size;
}
void *heap_core_alloc(struct heap_state *s, size_t size) {
    const size_t overhead = sizeof(struct heap_arena) + sizeof(struct block);
    if (!s || !s->stats.ready || !size || size > SIZE_MAX - overhead - 4095 - 15) return 0;
    size_t aligned = (size + 15) & ~(size_t)15;
    struct block *chosen = 0;
    for (struct heap_arena *a = s->first; a && !chosen; a = a->next)
        for (struct block *b = first(a); b; b = b->next)
            if (!b->requested && b->capacity >= aligned) { chosen = b; break; }
    if (!chosen) {
        size_t bytes = (aligned + overhead + 4095) & ~(size_t)4095;
        if (bytes < 16384) bytes = 16384;
        if (bytes > SIZE_MAX - s->stats.mapped_bytes) return 0;
        struct heap_arena *a = s->ops.acquire(bytes);
        if (!a) return 0;
        *a = (struct heap_arena){.next = s->first, .bytes = bytes};
        chosen = first(a);
        *chosen = (struct block){.capacity = bytes - overhead, .arena = a};
        s->first = a; s->stats.mapped_bytes += bytes; ++s->stats.arenas;
    }
    split(chosen, aligned); chosen->requested = size;
    ++s->stats.allocations; s->stats.requested_bytes += size;
    return chosen + 1;
}
static void merge_next(struct block *b) {
    struct block *next = b->next;
    if (!next || next->requested) return;
    b->capacity += sizeof *next + next->capacity; b->next = next->next;
    if (b->next) b->next->prev = b;
}
bool heap_core_free(struct heap_state *s, void *pointer) {
    if (!pointer) return true;
    struct block *b = find(s, pointer);
    if (!b) return false;
    --s->stats.allocations; s->stats.requested_bytes -= b->requested; b->requested = 0;
    merge_next(b);
    if (b->prev && !b->prev->requested) { b = b->prev; merge_next(b); }
    if (!b->prev && !b->next) {
        struct heap_arena *a = b->arena, **link = &s->first;
        while (*link != a) link = &(*link)->next;
        *link = a->next;
        size_t bytes = a->bytes;
        --s->stats.arenas; s->stats.mapped_bytes -= bytes;
        s->ops.release(a, bytes);
    }
    return true;
}
void *heap_core_calloc(struct heap_state *s, size_t count, size_t size) {
    if (!count || !size || count > SIZE_MAX / size) return 0;
    unsigned char *p = heap_core_alloc(s, count * size);
    if (p) for (size_t i = 0; i < count * size; ++i) p[i] = 0;
    return p;
}
void *heap_core_realloc(struct heap_state *s, void *pointer, size_t size) {
    if (!pointer) return heap_core_alloc(s, size);
    struct block *b = find(s, pointer);
    if (!b) return 0;
    if (!size) { heap_core_free(s, pointer); return 0; }
    if (size <= b->capacity) {
        s->stats.requested_bytes -= b->requested; s->stats.requested_bytes += size;
        b->requested = size; return pointer;
    }
    unsigned char *p = heap_core_alloc(s, size);
    if (!p) return 0;
    for (size_t i = 0; i < b->requested; ++i) p[i] = ((unsigned char *)pointer)[i];
    heap_core_free(s, pointer); return p;
}
