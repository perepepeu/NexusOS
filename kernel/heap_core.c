#include <heap_internal.h>
#include <stdint.h>

struct heap_arena { struct heap_arena *next; size_t bytes; uint64_t reserved[2]; };
struct block { size_t capacity, requested; struct block *prev, *next; struct heap_arena *arena; uint64_t reserved; };
#define MAGIC 0x4e45585553484541ull
#define REDZONE 16u
#define FREED 0xddu
_Static_assert(sizeof(struct heap_arena) % 16 == 0 && sizeof(struct block) % 16 == 0, "heap alignment");

static struct block *first(struct heap_arena *a) { return (struct block *)(a + 1); }
static unsigned char *raw(struct block *b) { return (unsigned char *)(b + 1); }
static void fill(unsigned char *p, size_t n, unsigned char value) {
    for (size_t i = 0; i < n; ++i) p[i] = value;
}
static void protect(struct block *b) {
    fill(raw(b), REDZONE, 0xa5);
    fill(raw(b) + REDZONE + b->requested, b->capacity - REDZONE - b->requested, 0x5a);
}
static enum heap_error bad(struct heap_state *s, enum heap_error error, uintptr_t address) {
    s->error = error; s->error_address = address; return error;
}
enum heap_error heap_core_check(struct heap_state *s) {
    if (!s || !s->stats.ready) return HEAP_OK;
    size_t arenas = 0, mapped = 0, allocations = 0, requested = 0;
    for (struct heap_arena *a = s->first; a; a = a->next) {
        if (++arenas > s->stats.arenas || a->reserved[0] != MAGIC || a->bytes < 4096 ||
                a->bytes % 4096 || (uintptr_t)a > UINTPTR_MAX - a->bytes)
            return bad(s, HEAP_CORRUPTION, (uintptr_t)a);
        uintptr_t end = (uintptr_t)a + a->bytes;
        struct block *prev = 0;
        for (struct block *b = first(a); b;) {
            uintptr_t at = (uintptr_t)b;
            if (at > end - sizeof *b || at % 16 || b->reserved != MAGIC ||
                    b->arena != a || b->prev != prev || b->capacity < 16 || b->capacity % 16 ||
                    b->capacity > end - at - sizeof *b ||
                    (b->requested && (b->capacity < 2 * REDZONE || b->requested > b->capacity - 2 * REDZONE)))
                return bad(s, HEAP_CORRUPTION, at);
            uintptr_t next = at + sizeof *b + b->capacity;
            if ((uintptr_t)b->next != (next == end ? 0 : next)) return bad(s, HEAP_CORRUPTION, at);
            for (size_t i = 0; i < b->capacity; ++i) {
                if (b->requested && i == REDZONE) { i += b->requested - 1; continue; }
                unsigned char expected = !b->requested ? FREED : i < REDZONE ? 0xa5 : 0x5a;
                if (raw(b)[i] != expected)
                    return bad(s, b->requested ? HEAP_CORRUPTION : HEAP_FREED_WRITE, (uintptr_t)(raw(b) + i));
            }
            if (b->requested) { ++allocations; requested += b->requested; }
            prev = b; b = b->next;
        }
        mapped += a->bytes;
    }
    if (arenas != s->stats.arenas || mapped != s->stats.mapped_bytes ||
            allocations != s->stats.allocations || requested != s->stats.requested_bytes)
        return bad(s, HEAP_CORRUPTION, (uintptr_t)s->first);
    return bad(s, HEAP_OK, 0);
}
static struct block *find(struct heap_state *s, void *p) {
    if (!s || !s->stats.ready) return 0;
    for (struct heap_arena *a = s->first; a; a = a->next)
        for (struct block *b = first(a); b; b = b->next)
            if ((void *)(raw(b) + REDZONE) == p && b->requested) return b;
    return 0;
}
bool heap_core_init(struct heap_state *s, struct heap_ops ops) {
    if (!s || s->stats.ready || !ops.acquire || !ops.release) return false;
    *s = (struct heap_state){.ops = ops, .stats.ready = true};
    return true;
}
static void split(struct block *b, size_t size) {
    if (b->capacity - size < sizeof(struct block) + 16) return;
    struct block *tail = (struct block *)((unsigned char *)(b + 1) + size);
    *tail = (struct block){.capacity = b->capacity - size - sizeof *tail,
        .prev = b, .next = b->next, .arena = b->arena, .reserved = MAGIC};
    fill(raw(tail), tail->capacity, FREED);
    if (tail->next) tail->next->prev = tail;
    b->next = tail; b->capacity = size;
}
void *heap_core_alloc(struct heap_state *s, size_t size) {
    const size_t overhead = sizeof(struct heap_arena) + sizeof(struct block) + 2 * REDZONE;
    if (!s || !s->stats.ready || !size || size > SIZE_MAX - overhead - 4095 - 15) return 0;
    if (heap_core_check(s) != HEAP_OK) return 0;
    size_t aligned = (size + 2 * REDZONE + 15) & ~(size_t)15;
    struct block *chosen = 0;
    for (struct heap_arena *a = s->first; a && !chosen; a = a->next)
        for (struct block *b = first(a); b; b = b->next)
            if (!b->requested && b->capacity >= aligned) { chosen = b; break; }
    if (!chosen) {
        size_t bytes = (aligned + overhead - 2 * REDZONE + 4095) & ~(size_t)4095;
        if (bytes < 16384) bytes = 16384;
        if (bytes > SIZE_MAX - s->stats.mapped_bytes) return 0;
        struct heap_arena *a = s->ops.acquire(bytes);
        if (!a) return 0;
        *a = (struct heap_arena){.next = s->first, .bytes = bytes, .reserved = {MAGIC, 0}};
        chosen = first(a);
        *chosen = (struct block){.capacity = bytes - overhead + 2 * REDZONE, .arena = a, .reserved = MAGIC};
        s->first = a; s->stats.mapped_bytes += bytes; ++s->stats.arenas;
    }
    split(chosen, aligned); chosen->requested = size;
    protect(chosen);
    ++s->stats.allocations; s->stats.requested_bytes += size;
    return raw(chosen) + REDZONE;
}
static void merge_next(struct block *b) {
    struct block *next = b->next;
    if (!next || next->requested) return;
    b->capacity += sizeof *next + next->capacity; b->next = next->next;
    if (b->next) b->next->prev = b;
}
bool heap_core_free(struct heap_state *s, void *pointer) {
    if (!pointer) return true;
    if (!s || heap_core_check(s) != HEAP_OK) return false;
    struct block *b = find(s, pointer);
    if (!b) { bad(s, HEAP_INVALID_POINTER, (uintptr_t)pointer); return false; }
    --s->stats.allocations; s->stats.requested_bytes -= b->requested; b->requested = 0;
    merge_next(b);
    if (b->prev && !b->prev->requested) { b = b->prev; merge_next(b); }
    fill(raw(b), b->capacity, FREED);
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
    if (!s || heap_core_check(s) != HEAP_OK) return 0;
    struct block *b = find(s, pointer);
    if (!b) { bad(s, HEAP_INVALID_POINTER, (uintptr_t)pointer); return 0; }
    if (!size) { heap_core_free(s, pointer); return 0; }
    if (size <= b->capacity - 2 * REDZONE) {
        s->stats.requested_bytes -= b->requested; s->stats.requested_bytes += size;
        b->requested = size; protect(b); return pointer;
    }
    unsigned char *p = heap_core_alloc(s, size);
    if (!p) return 0;
    for (size_t i = 0; i < b->requested; ++i) p[i] = ((unsigned char *)pointer)[i];
    heap_core_free(s, pointer); return p;
}
