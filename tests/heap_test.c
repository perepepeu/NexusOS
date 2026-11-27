#include <heap_internal.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t live_bytes, live_arenas;
static bool fail;
static void *acquire(size_t bytes) {
    assert(bytes && bytes % 4096 == 0);
    if (fail) return 0;
    void *p = aligned_alloc(4096, bytes); assert(p);
    live_bytes += bytes; ++live_arenas; return p;
}
static void release(void *p, size_t bytes) {
    assert(live_bytes >= bytes && live_arenas); live_bytes -= bytes; --live_arenas; free(p);
}
static void empty(struct heap_state *s) {
    assert(!live_bytes && !live_arenas && !s->first && !s->stats.allocations);
    assert(!s->stats.requested_bytes && !s->stats.mapped_bytes && !s->stats.arenas);
}
static uint32_t rng = 12345;
static uint32_t random_value(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(void) {
    struct heap_state s = {0};
    assert(!heap_core_alloc(&s, 16));
    assert(heap_core_init(&s, (struct heap_ops){acquire, release}));
    assert(!heap_core_init(&s, (struct heap_ops){acquire, release}));
    assert(!heap_core_alloc(&s, 0) && !heap_core_alloc(&s, SIZE_MAX));
    assert(!heap_core_calloc(&s, SIZE_MAX, 2) && !heap_core_calloc(&s, 0, 1));
    assert(heap_core_free(&s, 0));
    assert(!heap_core_free(&s, (void *)(uintptr_t)1));
    fail = true; assert(!heap_core_alloc(&s, 20)); fail = false; empty(&s);
    unsigned char *a = heap_core_alloc(&s, 1000), *b = heap_core_alloc(&s, 1000), *c = heap_core_alloc(&s, 1000);
    assert(a && b && c && (uintptr_t)a % 16 == 0);
    memset(a, 0x57, 1000);
    assert(heap_core_validate_pointer(&s, a));
    assert(!heap_core_validate_pointer(&s, a + 1));
    assert(heap_core_check(&s) == HEAP_OK);
    a[1000] = 0;
    assert(heap_core_check(&s) == HEAP_CORRUPTION && s.error_address == (uintptr_t)(a + 1000));
    assert(!heap_core_free(&s, a)); /* Corrupcao nao libera nem altera contadores. */
    a[1000] = 0x5a;
    a[-1] = 0;
    assert(heap_core_check(&s) == HEAP_CORRUPTION);
    a[-1] = 0xa5;
    assert(heap_core_check(&s) == HEAP_OK);
    assert(!heap_core_free(&s, a + 1));
    fail = true; assert(!heap_core_realloc(&s, a, 50000)); fail = false;
    for (size_t i = 0; i < 1000; ++i) assert(a[i] == 0x57);
    assert(heap_core_free(&s, b)); assert(!heap_core_free(&s, b));
    b[0] = 0;
    assert(heap_core_check(&s) == HEAP_FREED_WRITE && s.error_address == (uintptr_t)b);
    assert(!heap_core_alloc(&s, 10));
    b[0] = 0xdd;
    assert(heap_core_check(&s) == HEAP_OK);
    assert(heap_core_free(&s, a));
    void *d = heap_core_alloc(&s, 2000); assert(d == a && live_arenas == 1);
    assert(heap_core_free(&s, c) && heap_core_free(&s, d)); empty(&s);
    a = heap_core_calloc(&s, 12000, 3); assert(a);
    for (size_t i = 0; i < 36000; ++i) assert(a[i] == 0);
    memset(a, 9, 36000);
    b = heap_core_realloc(&s, a, 80000); assert(b);
    for (size_t i = 0; i < 36000; ++i) assert(b[i] == 9);
    assert(heap_core_realloc(&s, b, 30) == b);
    b[30] = 0;
    assert(heap_core_check(&s) == HEAP_CORRUPTION);
    b[30] = 0x5a;
    assert(!heap_core_realloc(&s, b, 0)); empty(&s);
    assert(!heap_core_free(&s, b));
    assert(!heap_core_realloc(&s, (void *)(uintptr_t)1, 20));
    a = heap_core_realloc(&s, 0, 17); assert(a); assert(heap_core_free(&s, a)); empty(&s);

    /* Interleaving reproducivel: detecta sobreposicao, corrupcao e contadores errados. */
    struct item { unsigned char *p; size_t size; unsigned char value; } items[64] = {0};
    for (size_t step = 0; step < 6000; ++step) {
        size_t slot = random_value() % 64;
        struct item *item = &items[slot];
        if (item->p) {
            for (size_t j = 0; j < item->size; ++j) assert(item->p[j] == item->value);
            if (random_value() & 1) {
                size_t size = random_value() % 20000 + 1;
                unsigned char *p = heap_core_realloc(&s, item->p, size); assert(p);
                for (size_t j = 0; j < (size < item->size ? size : item->size); ++j) assert(p[j] == item->value);
                item->p = p; item->size = size;
            } else { assert(heap_core_free(&s, item->p)); item->p = 0; }
        } else {
            item->size = random_value() % 30000 + 1; item->p = heap_core_alloc(&s, item->size);
            item->value = (unsigned char)random_value(); assert(item->p && !((uintptr_t)item->p % 16));
        }
        if (item->p) memset(item->p, item->value, item->size);
        size_t requested = 0, allocations = 0;
        for (size_t i = 0; i < 64; ++i) if (items[i].p) { requested += items[i].size; ++allocations; }
        assert(s.stats.allocations == allocations && s.stats.requested_bytes == requested);
        assert(s.stats.mapped_bytes == live_bytes && s.stats.arenas == live_arenas);
        assert(heap_core_check(&s) == HEAP_OK);
    }
    for (size_t i = 0; i < 64; ++i) if (items[i].p) {
        for (size_t j = 0; j < items[i].size; ++j) assert(items[i].p[j] == items[i].value);
        assert(heap_core_free(&s, items[i].p));
    }
    empty(&s); puts("OK: heap, overflow, OOM, realloc, coalescing, random stress and cleanup");
}
