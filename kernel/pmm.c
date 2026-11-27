#include <pmm_internal.h>
#include <io.h>
#include <log.h>
#include <panic.h>

static struct pmm_state allocator;
static uint64_t direct_map;

enum pmm_result pmm_init(uint64_t hhdm) {
    uint64_t flags = irq_save();
    enum pmm_result result = pmm_core_prepare(&allocator);
    if (result == PMM_OK) {
        for (unsigned int i = 0; i < allocator.stats.range_count; ++i) {
            uint64_t end = (allocator.ranges[i].first_page + allocator.ranges[i].pages) * MEMORY_PAGE_SIZE;
            if (end > UINT64_MAX - hhdm) { result = PMM_MAP_INVALID; break; }
        }
    }
    if (result == PMM_OK) {
        uint64_t address = allocator.stats.metadata_address;
        uint64_t bytes = allocator.stats.metadata_pages * MEMORY_PAGE_SIZE;
        if (address > UINT64_MAX - hhdm || bytes > UINT64_MAX - (hhdm + address)) result = PMM_MAP_INVALID;
        else {
            direct_map = hhdm;
            result = pmm_core_activate(&allocator, (void *)(uintptr_t)(hhdm + address), bytes);
        }
    }
    irq_restore(flags);
    return result;
}

uint64_t pmm_alloc_pages(uint64_t count, uint64_t alignment_pages) {
    uint64_t flags = irq_save();
    uint64_t address = pmm_core_alloc(&allocator, count, alignment_pages);
    irq_restore(flags);
    return address;
}
uint64_t pmm_alloc_page(void) { return pmm_alloc_pages(1, 1); }
uint64_t pmm_alloc_zeroed_page(void) {
    uint64_t address = pmm_alloc_page();
    if (address) {
        volatile uint8_t *page = (volatile uint8_t *)(uintptr_t)(direct_map + address);
        for (unsigned int i = 0; i < MEMORY_PAGE_SIZE; ++i) page[i] = 0;
    }
    return address;
}
enum pmm_result pmm_free_pages(uint64_t address, uint64_t count) {
    uint64_t flags = irq_save();
    enum pmm_result result = pmm_core_free(&allocator, address, count);
    irq_restore(flags);
    return result;
}
enum pmm_result pmm_free_page(uint64_t address) { return pmm_free_pages(address, 1); }
void pmm_get_stats(struct pmm_stats *out) {
    if (!out) return;
    uint64_t flags = irq_save();
    *out = allocator.stats;
    irq_restore(flags);
}
bool pmm_page_allocated(uint64_t address) {
    uint64_t flags = irq_save();
    bool allocated = pmm_core_page_allocated(&allocator, address);
    irq_restore(flags);
    return allocated;
}

void pmm_selftest(uint64_t hhdm) {
    struct pmm_stats before, after;
    pmm_get_stats(&before);
    KASSERT(before.ready && before.free_pages >= 16);
    uint64_t page = pmm_alloc_zeroed_page(), other = pmm_alloc_page();
    KASSERT(page && other && page != other && page % MEMORY_PAGE_SIZE == 0);
    volatile uint8_t *bytes = (volatile uint8_t *)(uintptr_t)(hhdm + page);
    for (unsigned int i = 0; i < MEMORY_PAGE_SIZE; ++i) KASSERT(bytes[i] == 0);
    bytes[0] = 0xa5; bytes[MEMORY_PAGE_SIZE - 1] = 0x5a;
    KASSERT(bytes[0] == 0xa5 && bytes[MEMORY_PAGE_SIZE - 1] == 0x5a);
    KASSERT(pmm_free_page(page + 1) == PMM_BAD_ARGUMENT);
    KASSERT(pmm_free_page(before.metadata_address) == PMM_METADATA);
    KASSERT(pmm_free_page(page) == PMM_OK && pmm_free_page(page) == PMM_NOT_ALLOCATED);
    KASSERT(pmm_free_page(other) == PMM_OK);
    uint64_t block = pmm_alloc_pages(3, 8);
    KASSERT(block && (block / MEMORY_PAGE_SIZE) % 8 == 0);
    KASSERT(pmm_free_pages(block, 3) == PMM_OK);
    uint64_t largest_low = 0, largest_high = 0;
    for (unsigned int i = 0; i < allocator.stats.range_count; ++i) {
        const struct pmm_range *range = &allocator.ranges[i];
        if (range->first_page >= 0x100000ull) {
            if (range->pages > largest_high) largest_high = range->pages;
        } else if (range->pages > largest_low) largest_low = range->pages;
    }
    if (largest_high > largest_low) {
        uint64_t pages = largest_low + 1;
        block = pmm_alloc_pages(pages, 1);
        KASSERT(block >= 0x100000000ull);
        volatile uint8_t *first = (volatile uint8_t *)(uintptr_t)(hhdm + block);
        volatile uint8_t *last = first + pages * MEMORY_PAGE_SIZE - 1;
        *first = 0x39; *last = 0x93;
        KASSERT(*first == 0x39 && *last == 0x93);
        KASSERT(pmm_free_pages(block, pages) == PMM_OK);
        log_info("test", "PMM acima de 4 GiB: escrita, leitura e liberacao OK.");
    }
    KASSERT(pmm_alloc_pages(before.total_pages + 1, 1) == 0);
    pmm_get_stats(&after);
    KASSERT(after.free_pages == before.free_pages && after.allocated_pages == before.allocated_pages);
    log_info("test", "PMM: alocacao, alinhamento, zero, liberacao e protecoes OK.");
}
