#ifndef NEXUS_ADDRESS_SPACE_H
#define NEXUS_ADDRESS_SPACE_H
#include <vmm_internal.h>
#include <stddef.h>
enum { USER_SPACE_MAX_PAGES = 64 };
#define USER_ADDRESS_END (1ull << 47)
#define USER_CODE_BASE 0x400000ull
#define USER_DATA_BASE 0x600000ull
#define USER_STACK_BASE 0x800000ull
#define USER_STACK_TOP (USER_STACK_BASE + 4 * 4096)
struct address_space {
    struct vmm_ops ops;
    void *context;
    uint64_t root, physical_mask;
    size_t pages, tables;
    bool ready;
};
bool address_core_create(struct address_space *, const struct vmm_ops *, void *, uint64_t kernel_root, uint64_t mask);
uint64_t address_core_alloc(struct address_space *, uint64_t va, unsigned int flags);
bool address_core_translate(struct address_space *, uint64_t va, struct vmm_mapping *out);
bool address_core_copy(struct address_space *, uint64_t va, void *buffer, size_t bytes, bool to_user);
/* Interface de syscall: preflight integral, USER em todos os niveis e WRITE
 * obrigatorio para copyout. Comprimento zero ignora o ponteiro. */
bool address_core_user_check(struct address_space *, uint64_t va, size_t bytes, bool writable);
bool address_core_user_copy(struct address_space *, uint64_t va, void *buffer, size_t bytes, bool to_user);
bool address_user_check(struct address_space *, uint64_t va, size_t bytes, bool writable);
bool address_user_copy(struct address_space *, uint64_t va, void *buffer, size_t bytes, bool to_user);
bool address_core_destroy(struct address_space *, uint64_t active_root);
/* Wrappers kernel: serializados com CLI em uma CPU. Copia e acesso sao do
 * loader/debugger confiavel, nao uma interface acessivel ao usuario. */
bool address_space_create(struct address_space *);
uint64_t address_space_alloc(struct address_space *, uint64_t va, unsigned int flags);
bool address_space_copy(struct address_space *, uint64_t va, void *buffer, size_t bytes, bool to_user);
bool address_space_destroy(struct address_space *);
uint64_t address_space_kernel_root(void);
void address_space_activate(uint64_t root);
#endif
