#include <address_space.h>
#include <io.h>
#include <panic.h>
bool address_space_create(struct address_space *s) {
    uint64_t flags = irq_save(), root, mask; struct vmm_ops ops;
    bool ok = vmm_kernel_context(&ops, &root, &mask) && address_core_create(s, &ops, 0, root, mask);
    irq_restore(flags); return ok;
}
uint64_t address_space_alloc(struct address_space *s, uint64_t va, unsigned int permissions) {
    uint64_t flags = irq_save(); uint64_t page = address_core_alloc(s, va, permissions); irq_restore(flags); return page;
}
bool address_space_copy(struct address_space *s, uint64_t va, void *buffer, size_t bytes, bool to_user) {
    uint64_t flags = irq_save(); bool ok = address_core_copy(s, va, buffer, bytes, to_user); irq_restore(flags); return ok;
}
bool address_space_destroy(struct address_space *s) {
    uint64_t flags = irq_save(), active;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(active));
    bool ok = address_core_destroy(s, active & 0x000ffffffffff000ull); irq_restore(flags); return ok;
}
uint64_t address_space_kernel_root(void) {
    uint64_t root, mask; struct vmm_ops ops;
    KASSERT(vmm_kernel_context(&ops, &root, &mask)); return root;
}
void address_space_activate(uint64_t root) {
    uint64_t flags = irq_save(), current;
    if (!root) root = address_space_kernel_root();
    KASSERT(root && !(root & 4095));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current));
    if ((current & 0x000ffffffffff000ull) != root) __asm__ volatile ("mov %0, %%cr3" : : "r"(root) : "memory");
    irq_restore(flags);
}
