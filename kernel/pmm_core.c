#include <pmm_internal.h>

static bool bit(const struct pmm_state *s, uint64_t index) {
    return (s->bitmap[index / 8] >> (index % 8)) & 1;
}
static void set_bit(struct pmm_state *s, uint64_t index, bool used) {
    uint8_t mask = (uint8_t)(1u << (index % 8));
    if (used) s->bitmap[index / 8] |= mask;
    else s->bitmap[index / 8] &= (uint8_t)~mask;
}

static void add_range(struct pmm_state *s, uint64_t base, uint64_t end) {
    uint64_t first = base / MEMORY_PAGE_SIZE + (base % MEMORY_PAGE_SIZE != 0);
    uint64_t last = end / MEMORY_PAGE_SIZE;
    if (!first) first = 1;
    if (last <= first) return;
    struct pmm_range *range = &s->ranges[s->stats.range_count++];
    range->first_page = first;
    range->pages = last - first;
    range->first_bit = s->stats.total_pages;
    s->stats.total_pages += range->pages;
}

enum pmm_result pmm_core_prepare(struct pmm_state *s) {
    if (!s) return PMM_BAD_ARGUMENT;
    if (s->stats.ready) return PMM_ALREADY_INITIALIZED;
    s->stats = (struct pmm_stats){0};
    s->bitmap = 0; s->hint = 0;
    const struct memory_stats *memory = memory_get_stats();
    if (!memory->valid) return PMM_MAP_INVALID;
    uint64_t base = 0, end = 0;
    bool active = false;
    for (unsigned int i = 0; i < memory->region_count; ++i) {
        const struct limine_memmap_entry *entry = memory_get_region(i);
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;
        if (active && entry->base == end) end = entry->base + entry->length;
        else {
            if (active) add_range(s, base, end);
            base = entry->base; end = entry->base + entry->length; active = true;
        }
    }
    if (active) add_range(s, base, end);
    if (s->stats.total_pages != memory->usable_pages) return PMM_MAP_INVALID;
    if (!s->stats.total_pages) return PMM_NO_MEMORY;
    s->stats.bitmap_bytes = s->stats.total_pages / 8 + (s->stats.total_pages % 8 != 0);
    s->stats.metadata_pages = s->stats.bitmap_bytes / MEMORY_PAGE_SIZE +
        (s->stats.bitmap_bytes % MEMORY_PAGE_SIZE != 0);
    for (unsigned int i = 0; i < s->stats.range_count; ++i) {
        if (s->ranges[i].pages >= s->stats.metadata_pages) {
            s->stats.metadata_address = s->ranges[i].first_page * MEMORY_PAGE_SIZE;
            return PMM_OK;
        }
    }
    return PMM_NO_MEMORY;
}

static const struct pmm_range *find_address(const struct pmm_state *s, uint64_t page) {
    unsigned int low = 0, high = s->stats.range_count;
    while (low < high) {
        unsigned int middle = low + (high - low) / 2;
        const struct pmm_range *range = &s->ranges[middle];
        if (page < range->first_page) high = middle;
        else if (page - range->first_page >= range->pages) low = middle + 1;
        else return range;
    }
    return 0;
}

enum pmm_result pmm_core_activate(struct pmm_state *s, void *bitmap, uint64_t capacity) {
    if (!s || !bitmap || !s->stats.total_pages || !s->stats.metadata_address) return PMM_BAD_ARGUMENT;
    if (s->stats.ready) return PMM_ALREADY_INITIALIZED;
    if (capacity < s->stats.bitmap_bytes) return PMM_BAD_ARGUMENT;
    const struct pmm_range *range = find_address(s, s->stats.metadata_address / MEMORY_PAGE_SIZE);
    if (!range) return PMM_MAP_INVALID;
    uint64_t offset = s->stats.metadata_address / MEMORY_PAGE_SIZE - range->first_page;
    if (s->stats.metadata_pages > range->pages - offset) return PMM_MAP_INVALID;
    s->bitmap = bitmap;
    for (uint64_t i = 0; i < s->stats.bitmap_bytes; ++i) ((volatile uint8_t *)s->bitmap)[i] = 0;
    for (uint64_t i = 0; i < s->stats.metadata_pages; ++i) set_bit(s, range->first_bit + offset + i, true);
    if (s->stats.total_pages % 8)
        s->bitmap[s->stats.bitmap_bytes - 1] |= (uint8_t)(0xffu << (s->stats.total_pages % 8));
    s->stats.free_pages = s->stats.total_pages - s->stats.metadata_pages;
    s->stats.allocated_pages = 0;
    s->stats.ready = true;
    return PMM_OK;
}

static uint64_t physical_for_bit(const struct pmm_state *s, uint64_t index) {
    unsigned int low = 0, high = s->stats.range_count;
    while (low < high) {
        unsigned int middle = low + (high - low) / 2;
        const struct pmm_range *range = &s->ranges[middle];
        if (index < range->first_bit) high = middle;
        else if (index - range->first_bit >= range->pages) low = middle + 1;
        else return (range->first_page + index - range->first_bit) * MEMORY_PAGE_SIZE;
    }
    return 0;
}

uint64_t pmm_core_alloc(struct pmm_state *s, uint64_t count, uint64_t alignment) {
    if (!s || !s->stats.ready || !count || !alignment || (alignment & (alignment - 1)) ||
            count > s->stats.free_pages) return 0;
    if (count == 1 && alignment == 1) {
        for (unsigned int pass = 0; pass < 2; ++pass) {
            uint64_t start = pass ? 0 : s->hint;
            uint64_t end = pass ? s->hint : s->stats.total_pages;
            for (uint64_t i = start; i < end; ++i) {
                if (!bit(s, i)) {
                    set_bit(s, i, true);
                    s->hint = i + 1 == s->stats.total_pages ? 0 : i + 1;
                    --s->stats.free_pages; ++s->stats.allocated_pages;
                    return physical_for_bit(s, i);
                }
            }
        }
        return 0;
    }
    for (unsigned int r = 0; r < s->stats.range_count; ++r) {
        const struct pmm_range *range = &s->ranges[r];
        uint64_t offset = 0;
        while (offset < range->pages) {
            uint64_t padding = (0ull - (range->first_page + offset)) & (alignment - 1);
            if (padding >= range->pages - offset) break;
            offset += padding;
            if (count > range->pages - offset) break;
            uint64_t free_run = 0;
            while (free_run < count && !bit(s, range->first_bit + offset + free_run)) ++free_run;
            if (free_run == count) {
                for (uint64_t i = 0; i < count; ++i) set_bit(s, range->first_bit + offset + i, true);
                s->stats.free_pages -= count; s->stats.allocated_pages += count;
                return (range->first_page + offset) * MEMORY_PAGE_SIZE;
            }
            offset += free_run + 1;
        }
    }
    return 0;
}

enum pmm_result pmm_core_free(struct pmm_state *s, uint64_t address, uint64_t count) {
    if (!s || !s->stats.ready) return PMM_NOT_READY;
    if (!address || address % MEMORY_PAGE_SIZE || !count) return PMM_BAD_ARGUMENT;
    uint64_t page = address / MEMORY_PAGE_SIZE;
    const struct pmm_range *range = find_address(s, page);
    if (!range || count > range->pages - (page - range->first_page)) return PMM_INVALID_ADDRESS;
    uint64_t end = address + count * MEMORY_PAGE_SIZE;
    uint64_t metadata_end = s->stats.metadata_address + s->stats.metadata_pages * MEMORY_PAGE_SIZE;
    if (address < metadata_end && end > s->stats.metadata_address) return PMM_METADATA;
    uint64_t start = range->first_bit + page - range->first_page;
    for (uint64_t i = 0; i < count; ++i) if (!bit(s, start + i)) return PMM_NOT_ALLOCATED;
    for (uint64_t i = 0; i < count; ++i) set_bit(s, start + i, false);
    s->stats.free_pages += count; s->stats.allocated_pages -= count;
    return PMM_OK;
}
