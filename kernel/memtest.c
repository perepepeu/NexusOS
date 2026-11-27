#include <memory.h>
#include <pmm.h>
#include <vmm.h>
#include <heap.h>
#include <panic.h>

enum { TEST_PAGES = 64 };
static bool usable(uint64_t physical) {
    for (unsigned int i = 0; i < memory_get_stats()->region_count; ++i) {
        const struct limine_memmap_entry *e = memory_get_region(i);
        if (e->type == LIMINE_MEMMAP_USABLE && physical >= e->base &&
                physical - e->base <= e->length && e->length - (physical - e->base) >= 4096) return true;
    }
    return false;
}

bool memory_test_run(void) {
    struct pmm_stats pb, pa;
    struct vmm_stats vb, va;
    struct heap_stats hb, ha;
    pmm_get_stats(&pb); vmm_get_stats(&vb); heap_get_stats(&hb);
    if (!pb.ready || !vb.ready || !hb.ready) return false;
    /* Janela reservada ao teste, distinta das paginas dos selftests e do heap. */
    uint64_t base = vb.window_base + 16 * 1024 * 1024, frames[TEST_PAGES] = {0};
    unsigned int mapped = 0;
    bool ok = true;
    unsigned char *buffer = 0, *zero = 0;
    for (unsigned int i = 0; i < TEST_PAGES; ++i) {
        struct vmm_mapping existing;
        if (vmm_translate(base + i * 4096, &existing)) { ok = false; goto cleanup; }
    }
    for (; mapped < TEST_PAGES; ++mapped) {
        uint64_t address = base + mapped * 4096;
        if (vmm_alloc_page(address, VMM_WRITE, &frames[mapped]) != VMM_OK) { ok = false; goto cleanup; }
        if (!usable(frames[mapped]) || !pmm_page_allocated(frames[mapped])) ok = false;
        volatile uint64_t *words = (volatile uint64_t *)(uintptr_t)address;
        for (unsigned int j = 0; j < 512; ++j) {
            if (words[j] != 0) ok = false;
            words[j] = 0xa55a000000000000ull ^ ((uint64_t)mapped << 32) ^ j;
        }
    }
    for (unsigned int i = 0; i < mapped; ++i) {
        volatile uint64_t *words = (volatile uint64_t *)(uintptr_t)(base + i * 4096);
        for (unsigned int j = 0; j < 512; ++j)
            if (words[j] != (0xa55a000000000000ull ^ ((uint64_t)i << 32) ^ j)) ok = false;
    }
    buffer = kmalloc(257); zero = kcalloc(31, 257);
    if (!buffer || !zero) { ok = false; goto cleanup; }
    for (unsigned int i = 0; i < 31 * 257; ++i) if (zero[i]) ok = false;
    for (unsigned int i = 0; i < 257; ++i) buffer[i] = (unsigned char)i;
    unsigned char *grown = krealloc(buffer, 16385);
    if (!grown) { ok = false; goto cleanup; }
    buffer = grown;
    for (unsigned int i = 0; i < 257; ++i) if (buffer[i] != (unsigned char)i) ok = false;
    heap_check();
cleanup:
    kfree(buffer); kfree(zero);
    for (unsigned int i = 0; i < mapped; ++i) {
        uint64_t physical;
        KASSERT(vmm_unmap_page(base + i * 4096, &physical) == VMM_OK && physical == frames[i]);
        KASSERT(pmm_free_page(physical) == PMM_OK);
    }
    heap_check();
    pmm_get_stats(&pa); vmm_get_stats(&va); heap_get_stats(&ha);
    return ok && pb.free_pages == pa.free_pages && pb.allocated_pages == pa.allocated_pages &&
        vb.mapped_pages == va.mapped_pages && vb.table_pages == va.table_pages &&
        hb.allocations == ha.allocations && hb.arenas == ha.arenas &&
        hb.requested_bytes == ha.requested_bytes && hb.mapped_bytes == ha.mapped_bytes;
}
