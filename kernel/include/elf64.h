#ifndef NEXUS_ELF64_H
#define NEXUS_ELF64_H
#include <address_space.h>
enum { ELF64_MAX_PHDR = 32, ELF64_MAX_LOAD = 16, ELF64_MAX_FILE = 1024 * 1024, ELF64_ARG0_MAX = 63 };
enum elf64_result {
    ELF64_OK, ELF64_ARGUMENT, ELF64_HEADER, ELF64_UNSUPPORTED, ELF64_TRUNCATED,
    ELF64_SEGMENT, ELF64_PERMISSIONS, ELF64_OVERLAP, ELF64_LIMIT, ELF64_ENTRY, ELF64_MEMORY, ELF64_STATE, ELF64_IO
};
struct elf64_header {
    unsigned char ident[16];
    uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct elf64_phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
struct elf64_segment { uint64_t offset, vaddr, filesz, memsz, first, end; unsigned int permissions; };
struct elf64_plan { uint64_t entry; size_t pages; unsigned int count; struct elf64_segment segments[ELF64_MAX_LOAD]; };
/* Estatico ET_EXEC/SYSV/AMD64, sem interpretador, TLS ou relocacao dinamica.
 * Intervalos de PT_LOAD nao podem compartilhar paginas ou invadir a stack. */
enum elf64_result elf64_validate(const void *image, size_t bytes, struct elf64_plan *out);
/* Cria um espaco novo/inativo. Falhas liberam TODAS as paginas/tabelas.
 * ops.allocate deve devolver paginas zeradas, como no VMM/PMM do kernel. */
enum elf64_result elf64_build(struct address_space *, const struct vmm_ops *, void *context,
    uint64_t kernel_root, uint64_t physical_mask, const void *image, size_t bytes,
    const char *name, uint64_t *entry, uint64_t *stack);
const char *elf64_error(enum elf64_result);
uint64_t process_create_elf(const char *name, const void *image, size_t bytes, enum elf64_result *error);
#endif
