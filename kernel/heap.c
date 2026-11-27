#include <heap_internal.h>
#include <vmm.h>
#include <pmm.h>
#include <io.h>
#include <panic.h>
#include <log.h>
#include <stdint.h>

#define HEAP_PAGES 16384u
static struct heap_state heap;
static uint64_t base;
static unsigned char used[HEAP_PAGES];
/* Injecao local, habilitada somente durante selftest=heap. */
static int fail_after = -1;

static void unmap_data(void *pointer, size_t bytes) {
    size_t start = ((uintptr_t)pointer - base) / 4096, pages = bytes / 4096;
    for (size_t i = 0; i < pages; ++i) {
        uint64_t physical;
        KASSERT(vmm_unmap_page(base + (start + i) * 4096, &physical) == VMM_OK);
        KASSERT(pmm_free_page(physical) == PMM_OK);
        used[start + i] = 0;
    }
}
static void release(void *pointer, size_t bytes) {
    size_t start = ((uintptr_t)pointer - base) / 4096;
    unmap_data(pointer, bytes);
    used[start - 1] = 0; used[start + bytes / 4096] = 0;
}
static void *acquire(size_t bytes) {
    size_t pages = bytes / 4096, run = 0;
    if (!pages || pages > HEAP_PAGES - 2) return 0;
    for (size_t i = 0; i < HEAP_PAGES; ++i) {
        run = used[i] ? 0 : run + 1;
        if (run < pages + 2) continue;
        size_t start = i - pages, mapped = 0;
        void *pointer = (void *)(uintptr_t)(base + start * 4096);
        for (; mapped < pages; ++mapped) {
            if (fail_after == 0) break;
            if (fail_after > 0) --fail_after;
            uint64_t physical;
            if (vmm_alloc_page(base + (start + mapped) * 4096, VMM_WRITE, &physical) != VMM_OK) break;
            used[start + mapped] = 1;
        }
        if (mapped != pages) { unmap_data(pointer, mapped * 4096); return 0; }
        used[start - 1] = 2; used[start + pages] = 2;
        return pointer;
    }
    return 0;
}
const char *heap_address_kind(uint64_t address) {
    if (!heap.stats.ready || address < base || address - base >= HEAP_PAGES * 4096ull) return "fora do heap";
    unsigned char kind = used[(address - base) / 4096];
    return kind == 2 ? "pagina de guarda do heap" : kind == 1 ? "pagina de dados do heap" : "pagina livre do heap";
}
static void verify_result(void) {
    if (heap.error == HEAP_OK) return;
    const char *reason = heap.error == HEAP_INVALID_POINTER ? "ponteiro invalido ou double free" :
        heap.error == HEAP_FREED_WRITE ? "escrita em bloco livre (use-after-free)" : "limites ou metadados do heap corrompidos";
    kernel_memory_panic(reason, heap.error_address, __FILE__, __LINE__);
}
void heap_check(void) {
    uint64_t flags = irq_save(); heap_core_check(&heap); verify_result(); irq_restore(flags);
}
bool heap_init(void) {
    struct vmm_stats v;
    vmm_get_stats(&v);
    if (heap.stats.ready || !v.ready || v.window_size < (1ull << 30) + HEAP_PAGES * 4096ull) return false;
    base = v.window_base + (1ull << 30);
    return heap_core_init(&heap, (struct heap_ops){acquire, release});
}
void *kmalloc(size_t size) {
    uint64_t flags = irq_save(); void *p = heap_core_alloc(&heap, size); verify_result(); irq_restore(flags); return p;
}
void *kcalloc(size_t count, size_t size) {
    uint64_t flags = irq_save(); void *p = heap_core_calloc(&heap, count, size); verify_result(); irq_restore(flags); return p;
}
void *krealloc(void *pointer, size_t size) {
    uint64_t flags = irq_save();
    void *p = heap_core_realloc(&heap, pointer, size); verify_result(); irq_restore(flags); return p;
}
void kfree(void *pointer) {
    uint64_t flags = irq_save(); heap_core_free(&heap, pointer); verify_result(); irq_restore(flags);
}
void heap_access_selftest(unsigned int scenario) {
    if (scenario == 1) {
        log_info("test", "Acesso invalido em %p", (void *)0);
        __asm__ volatile ("movb (%0), %%al" : : "r"((uintptr_t)0) : "rax", "memory");
    }
    volatile unsigned char *a = kmalloc(31);
    KASSERT(a);
    uint64_t fault = 0;
    if (scenario == 2) fault = base;
    if (scenario == 3) fault = base + 5 * 4096;
    if (scenario == 4) { fault = (uintptr_t)a; kfree((void *)a); }
    if (fault) {
        log_info("test", "Acesso invalido em %p", (void *)(uintptr_t)fault);
        if (scenario == 4) __asm__ volatile ("movb (%0), %%al" : : "r"(fault) : "rax", "memory");
        else __asm__ volatile ("movb $1, (%0)" : : "r"(fault) : "memory");
    }
    if (scenario == 5) { a[31] = 0; kfree((void *)a); }
    if (scenario == 6) { a[-1] = 0; heap_check(); }
    if (scenario == 7 || scenario == 8) {
        KASSERT(kmalloc(31)); /* Mantem a arena mapeada apos liberar a. */
        kfree((void *)a);
        if (scenario == 7) { a[0] = 0; heap_check(); }
        else kfree((void *)a);
    }
    if (scenario == 9) kfree((void *)(a + 1));
    if (scenario == 10) {
        a[0] = 0xc3;
        log_info("test", "Acesso invalido em %p", (void *)a);
        ((void (*)(void))(uintptr_t)a)();
    }
    PANIC(ERR_SELFTEST, "Acesso invalido nao foi detectado.");
}
void heap_get_stats(struct heap_stats *out) {
    if (!out) return;
    uint64_t flags = irq_save(); *out = heap.stats; irq_restore(flags);
}
void heap_selftest(void) {
    struct pmm_stats before, after;
    struct vmm_stats vb, va;
    pmm_get_stats(&before); vmm_get_stats(&vb);
    fail_after = 2;
    KASSERT(!kmalloc(20000));
    fail_after = -1;
    pmm_get_stats(&after); vmm_get_stats(&va);
    KASSERT(before.free_pages == after.free_pages && vb.mapped_pages == va.mapped_pages && vb.table_pages == va.table_pages);
    unsigned char *a = kmalloc(31), *b = kcalloc(4097, 3), *c = kmalloc(200000);
    KASSERT(a && b && c && !((uintptr_t)a % 16) && !((uintptr_t)b % 16));
    for (size_t i = 0; i < 12291; ++i) KASSERT(b[i] == 0);
    for (size_t i = 0; i < 31; ++i) a[i] = (unsigned char)i;
    for (size_t i = 0; i < 200000; ++i) c[i] = (unsigned char)(i * 7);
    unsigned char *grown = krealloc(a, 32769); KASSERT(grown);
    for (size_t i = 0; i < 31; ++i) KASSERT(grown[i] == i);
    KASSERT(!krealloc(grown, SIZE_MAX) && !kmalloc(0) && !kcalloc(SIZE_MAX, 2));
    KASSERT(!kmalloc(HEAP_PAGES * 4096ull)); /* Limite virtual, incluindo metadados/guardas. */
    for (size_t i = 0; i < 200000; ++i) KASSERT(c[i] == (unsigned char)(i * 7));
    struct vmm_mapping mapping;
    KASSERT(vmm_translate((uintptr_t)c, &mapping) && mapping.flags == VMM_WRITE);
    kfree(b); kfree(c); kfree(grown); kfree(0);
    /* Fragmentacao e fusao dos blocos dentro de uma arena. */
    a = kmalloc(1024); b = kmalloc(1024); c = kmalloc(1024);
    KASSERT(a && b && c); kfree(b); kfree(a);
    unsigned char *merged = kmalloc(2000); KASSERT(merged == a);
    kfree(merged); kfree(c);
    struct heap_stats h; heap_get_stats(&h);
    pmm_get_stats(&after); vmm_get_stats(&va);
    KASSERT(!h.allocations && !h.arenas && !h.mapped_bytes && !h.requested_bytes);
    KASSERT(before.free_pages == after.free_pages && vb.mapped_pages == va.mapped_pages && vb.table_pages == va.table_pages);
    log_info("test", "Heap: alinhamento, zero, realloc, fusao e devolucao ao PMM/VMM OK.");
}
