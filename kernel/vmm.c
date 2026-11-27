#include <vmm_internal.h>
#include <pmm.h>
#include <io.h>
#include <log.h>
#include <panic.h>
#include <memory.h>

static struct vmm_state manager;
static uint64_t direct_map;

static uint64_t allocate(void *context) { (void)context; return pmm_alloc_zeroed_page(); }
static void release(void *context, uint64_t physical) {
    (void)context;
    KASSERT(pmm_free_page(physical) == PMM_OK);
}
static uint64_t *table(void *context, uint64_t physical) {
    (void)context;
    return physical && physical <= UINT64_MAX - direct_map ? (uint64_t *)(uintptr_t)(direct_map + physical) : 0;
}
static bool allocated(void *context, uint64_t physical) { (void)context; return pmm_page_allocated(physical); }
static void invalidate(void *context, uint64_t va) {
    (void)context;
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}
static void flush(void *context) {
    (void)context;
    uint64_t root;
    __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3" : "=r"(root) : : "memory");
}

enum vmm_result vmm_init(uint64_t hhdm) {
    if (manager.stats.ready) return VMM_ALREADY_INITIALIZED;
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    uint32_t maximum = a;
    if (maximum < 0x80000001u) return VMM_UNSUPPORTED;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001u), "c"(0));
    if (!(d & (1u << 20))) return VMM_UNSUPPORTED;
    unsigned int bits = 36;
    if (maximum >= 0x80000008u) {
        __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000008u), "c"(0));
        bits = a & 255;
    }
    uint64_t flags = irq_save(), cr3, cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & ((1u << 12) | (1u << 17))) { irq_restore(flags); return VMM_UNSUPPORTED; }
    direct_map = hhdm;
    struct vmm_ops ops = {allocate, release, table, allocated, invalidate, flush};
    enum vmm_result result = vmm_core_init(&manager, &ops, 0, cr3 & 0x000ffffffffff000ull, bits);
    if (result == VMM_OK) {
        uint32_t low, high;
        __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080u));
        low |= 1u << 11;
        __asm__ volatile ("wrmsr" : : "a"(low), "d"(high), "c"(0xc0000080u) : "memory");
        uint64_t cr0;
        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 1u << 16;
        __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
        uint64_t root = manager.stats.root_address | (cr3 & 0x18);
        __asm__ volatile ("mov %0, %%cr3" : : "r"(root) : "memory");
    }
    irq_restore(flags);
    return result;
}

enum vmm_result vmm_map_page(uint64_t va, uint64_t physical, unsigned int permissions) {
    uint64_t flags = irq_save();
    enum vmm_result result = vmm_core_map(&manager, va, physical, permissions);
    irq_restore(flags); return result;
}
enum vmm_result vmm_alloc_page(uint64_t va, unsigned int permissions, uint64_t *physical) {
    uint64_t flags = irq_save();
    enum vmm_result result = vmm_core_alloc(&manager, va, permissions, physical);
    irq_restore(flags); return result;
}
enum vmm_result vmm_unmap_page(uint64_t va, uint64_t *physical) {
    uint64_t flags = irq_save();
    enum vmm_result result = vmm_core_unmap(&manager, va, physical);
    irq_restore(flags); return result;
}
enum vmm_result vmm_protect_page(uint64_t va, unsigned int permissions) {
    uint64_t flags = irq_save();
    enum vmm_result result = vmm_core_protect(&manager, va, permissions);
    irq_restore(flags); return result;
}
bool vmm_translate(uint64_t va, struct vmm_mapping *mapping) {
    uint64_t flags = irq_save();
    bool present = vmm_core_translate(&manager, va, mapping);
    irq_restore(flags); return present;
}
void vmm_get_stats(struct vmm_stats *stats) {
    if (!stats) return;
    uint64_t flags = irq_save(); *stats = manager.stats; irq_restore(flags);
}
bool vmm_kernel_context(struct vmm_ops *ops, uint64_t *root, uint64_t *mask) {
    uint64_t flags = irq_save();
    bool ok = manager.stats.ready;
    if (ok) { *ops = manager.ops; *root = manager.stats.root_address; *mask = manager.physical_mask; }
    irq_restore(flags); return ok;
}

void vmm_selftest(uint64_t hhdm, unsigned int fault) {
    struct pmm_stats before, after;
    struct vmm_stats initial, final;
    pmm_get_stats(&before); vmm_get_stats(&initial);
    uint64_t base = initial.window_base, a, b, alias;
    struct vmm_mapping mapping;
    KASSERT(vmm_translate((uintptr_t)vmm_selftest, &mapping) && (mapping.flags & VMM_EXEC));
    KASSERT(vmm_alloc_page(base + 0x2000, VMM_WRITE, &a) == VMM_OK);
    KASSERT(vmm_alloc_page(base + 0x3000, VMM_WRITE, &b) == VMM_OK && a != b);
    volatile uint64_t *first = (volatile uint64_t *)(uintptr_t)(base + 0x2000);
    volatile uint64_t *second = (volatile uint64_t *)(uintptr_t)(base + 0x3000);
    KASSERT(*first == 0 && *second == 0);
    *first = 0x1122334455667788ull; *second = 0x8877665544332211ull;
    KASSERT(*first != *second);
    KASSERT(vmm_translate(base + 0x207b, &mapping) && mapping.physical == a + 123 &&
        mapping.page_size == 4096 && mapping.flags == VMM_WRITE);
    KASSERT(vmm_translate(hhdm + a, &mapping) && mapping.physical == a);
    KASSERT(vmm_map_page(base + 0x4000, a, VMM_WRITE) == VMM_OK);
    volatile uint64_t *third = (volatile uint64_t *)(uintptr_t)(base + 0x4000);
    *third = 7; KASSERT(*first == 7 && *second == 0x8877665544332211ull);
    KASSERT(vmm_map_page(base + 0x4000, b, 0) == VMM_ALREADY_MAPPED);
    KASSERT(vmm_protect_page(base + 0x4000, 0) == VMM_OK);
    KASSERT(vmm_translate(base + 0x4000, &mapping) && mapping.flags == 0);
    *first = 9; KASSERT(*third == 9);
    KASSERT(vmm_unmap_page(base + 0x4000, &alias) == VMM_OK && alias == a && pmm_page_allocated(a));
    KASSERT(vmm_unmap_page(base + 0x4000, 0) == VMM_NOT_MAPPED);
    KASSERT(vmm_map_page((uintptr_t)vmm_selftest & ~4095ull, a, 0) == VMM_PROTECTED);
    KASSERT(vmm_map_page(0x0000800000000000ull, a, 0) == VMM_BAD_ADDRESS);
    KASSERT(vmm_map_page(base + 1, a, 0) == VMM_BAD_ADDRESS);
    KASSERT(vmm_map_page(base, a, VMM_WRITE | VMM_EXEC) == VMM_BAD_FLAGS);
    KASSERT(vmm_unmap_page(base + 0x2000, 0) == VMM_OK && pmm_free_page(a) == PMM_OK);
    KASSERT(vmm_unmap_page(base + 0x3000, 0) == VMM_OK && pmm_free_page(b) == PMM_OK);
    KASSERT(vmm_alloc_page(base + 0x4000, VMM_WRITE, &a) == VMM_OK);
    *(volatile uint8_t *)(uintptr_t)(base + 0x4000) = 0xc3; /* RET */
    KASSERT(vmm_protect_page(base + 0x4000, VMM_EXEC) == VMM_OK);
    ((void (*)(void))(uintptr_t)(base + 0x4000))();
    KASSERT(vmm_unmap_page(base + 0x4000, 0) == VMM_OK && pmm_free_page(a) == PMM_OK);
    uint64_t low_pages = 0, high_pages = 0;
    for (unsigned int i = 0; i < memory_get_stats()->region_count; ++i) {
        const struct limine_memmap_entry *entry = memory_get_region(i);
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t count = entry->length / 4096;
        if (entry->base >= 0x100000000ull) { if (count > high_pages) high_pages = count; }
        else if (count > low_pages) low_pages = count;
    }
    if (high_pages > low_pages) {
        uint64_t count = low_pages + 1;
        a = pmm_alloc_pages(count, 1);
        KASSERT(a >= 0x100000000ull);
        KASSERT(vmm_map_page(base + 0x6000, a, VMM_WRITE) == VMM_OK);
        *(volatile uint64_t *)(uintptr_t)(base + 0x6000) = 0x123456789abcdef0ull;
        KASSERT(*(volatile uint64_t *)(uintptr_t)(hhdm + a) == 0x123456789abcdef0ull);
        KASSERT(vmm_unmap_page(base + 0x6000, 0) == VMM_OK && pmm_free_pages(a, count) == PMM_OK);
        log_info("test", "VMM: frame acima de 4 GiB mapeado e acessado OK.");
    }
    pmm_get_stats(&after); vmm_get_stats(&final);
    KASSERT(before.free_pages == after.free_pages && before.allocated_pages == after.allocated_pages);
    KASSERT(final.mapped_pages == initial.mapped_pages && final.table_pages == initial.table_pages);
    log_info("test", "VMM: CR3, aliases, isolamento, permissoes e limpeza OK.");
    if (fault) {
        uint64_t va = base + 0x5000;
        KASSERT(vmm_alloc_page(va, VMM_WRITE, &a) == VMM_OK);
        if (fault == 1) {
            KASSERT(vmm_unmap_page(va, 0) == VMM_OK && pmm_free_page(a) == PMM_OK);
            (void)*(volatile uint64_t *)(uintptr_t)va;
        } else if (fault == 2) {
            KASSERT(vmm_protect_page(va, 0) == VMM_OK);
            *(volatile uint64_t *)(uintptr_t)va = 123;
        } else {
            *(volatile uint8_t *)(uintptr_t)va = 0xc3;
            KASSERT(vmm_protect_page(va, 0) == VMM_OK);
            ((void (*)(void))(uintptr_t)va)();
        }
        PANIC(ERR_SELFTEST, "Protecao virtual nao gerou page fault.");
    }
}
