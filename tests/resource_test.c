#include <resource_internal.h>
#include <assert.h>
#include <stdio.h>
static struct resource_registry registry;
static size_t released[4], custom;
static void cleanup(void *p) { assert(p == &custom); ++custom; }
static void dispose(const struct resource_entry *entry) {
    assert(registry.draining && resource_reserve(&registry, 2) < 0);
    assert(resource_reap(&registry, 2, dispose) == 0);
    assert(entry->owner < 4); ++released[entry->owner];
    if (entry->release) entry->release((void *)entry->address);
}
int main(void) {
    int a = resource_reserve(&registry, 2), b = resource_reserve(&registry, 3);
    assert(a >= 0 && b >= 0 && resource_count(&registry, 2) == 1);
    assert(resource_commit(&registry, a, 0x1000, RESOURCE_HEAP, 31, 0));
    assert(!resource_commit(&registry, b, 0x1000, RESOURCE_HEAP, 4, 0));
    assert(!resource_commit(&registry, b, 0x2000, RESOURCE_CUSTOM, 1, 0));
    assert(resource_commit(&registry, b, (uintptr_t)&custom, RESOURCE_CUSTOM, 1, cleanup));
    assert(resource_commit(&registry, a, 0x3000, RESOURCE_HEAP, 8193, 0));
    assert(resource_find(&registry, 0x1000) < 0 && resource_find(&registry, 0x3000) == a);
    resource_cancel(&registry, a); assert(resource_find(&registry, 0x3000) == a);
    assert(resource_release(&registry, a, dispose) && !resource_release(&registry, a, dispose));
    assert(released[2] == 1 && !released[3]);
    for (unsigned int i = 0; i < RESOURCE_MAX - 1; ++i) assert(resource_reserve(&registry, 2) >= 0);
    assert(resource_reserve(&registry, 2) < 0);
    assert(resource_reap(&registry, 2, dispose) == 0); /* Reservas sem alocacao nao tem destrutor. */
    assert(!resource_count(&registry, 2) && resource_count(&registry, 3) == 1);
    assert(resource_reap(&registry, 3, dispose) == 1 && custom == 1);
    assert(!resource_reap(&registry, 3, dispose));
    for (unsigned int i = 0; i < RESOURCE_MAX; ++i) {
        int token = resource_reserve(&registry, 2); assert(token >= 0);
        assert(resource_commit(&registry, token, 0x10000 + i * 4096, i % 2 ? RESOURCE_MAPPING : RESOURCE_PAGES, i + 1, 0));
    }
    assert(resource_reserve(&registry, 2) < 0 && resource_count(&registry, 2) == RESOURCE_MAX);
    assert(resource_reap(&registry, 2, dispose) == RESOURCE_MAX && released[2] == RESOURCE_MAX + 1);
    assert(!resource_count(&registry, 2) && resource_reserve(&registry, 0) < 0);
    assert(resource_find(&registry, 0) < 0 && !resource_release(&registry, -1, dispose));
    puts("OK: recursos, reservas/OOM, duplicatas, realloc, donos, reentrada e limpeza unica.");
}
