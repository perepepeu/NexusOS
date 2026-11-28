#include <elf64.h>
_Static_assert(sizeof(struct elf64_header) == 64, "ELF64 header");
_Static_assert(sizeof(struct elf64_phdr) == 56, "ELF64 program header");
static void copy_bytes(void *to, const void *from, size_t bytes) {
    unsigned char *d = to; const unsigned char *s = from;
    for (size_t i = 0; i < bytes; ++i) d[i] = s[i];
}
enum elf64_result elf64_validate(const void *image, size_t bytes, struct elf64_plan *out) {
    if (!out) return ELF64_ARGUMENT;
    *out = (struct elf64_plan){0};
    if (!image) return ELF64_ARGUMENT;
    if (bytes > ELF64_MAX_FILE) return ELF64_LIMIT;
    if (bytes < sizeof(struct elf64_header)) return ELF64_TRUNCATED;
    struct elf64_header h; copy_bytes(&h, image, sizeof h);
    if (h.ident[0] != 0x7f || h.ident[1] != 'E' || h.ident[2] != 'L' || h.ident[3] != 'F') return ELF64_HEADER;
    if (h.ident[4] != 2 || h.ident[5] != 1 || h.ident[6] != 1 || h.ident[7] || h.ident[8] ||
            h.type != 2 || h.machine != 62 || h.version != 1 || h.flags) return ELF64_UNSUPPORTED;
    if (h.ehsize != sizeof h || h.phentsize != sizeof(struct elf64_phdr) || !h.phnum) return ELF64_HEADER;
    if (h.phnum > ELF64_MAX_PHDR) return ELF64_LIMIT;
    if (h.phoff < sizeof h || h.phoff > bytes || h.phnum > (bytes - h.phoff) / sizeof(struct elf64_phdr)) return ELF64_TRUNCATED;
    /* Sections nao sao usadas para mapear memoria. Se presentes, validar o
     * intervalo declarado; contagens estendidas nao fazem parte deste loader. */
    if (h.shnum) {
        if (h.shentsize != 64 || h.shoff < sizeof h || h.shoff > bytes ||
            h.shnum > (bytes - h.shoff) / 64 || h.shstrndx >= h.shnum) return ELF64_HEADER;
    } else if (h.shoff || h.shstrndx || (h.shentsize && h.shentsize != 64)) return ELF64_UNSUPPORTED;
    struct elf64_plan plan = {.entry = h.entry}; bool executable_entry = false;
    for (unsigned int i = 0; i < h.phnum; ++i) {
        struct elf64_phdr p;
        copy_bytes(&p, (const unsigned char *)image + h.phoff + i * sizeof p, sizeof p);
        /* PT_DYNAMIC, PT_INTERP e PT_TLS exigem runtime nao implementado. */
        if (p.type == 2 || p.type == 3 || p.type == 7) return ELF64_UNSUPPORTED;
        if (p.type == 0x6474e551 && (p.flags & 1)) return ELF64_PERMISSIONS; /* GNU_STACK executavel. */
        if (p.type != 1) {
            /* Metadados conhecidos, sem efeito de mapeamento. */
            if (p.type != 0 && p.type != 4 && p.type != 6 && p.type != 0x6474e550 && p.type != 0x6474e551 &&
                    p.type != 0x6474e552 && p.type != 0x6474e553) return ELF64_UNSUPPORTED;
            if (p.offset > bytes || p.filesz > bytes - p.offset) return ELF64_TRUNCATED;
            continue;
        }
        if (p.filesz > p.memsz) return ELF64_SEGMENT;
        if (p.offset > bytes || p.filesz > bytes - p.offset) return ELF64_TRUNCATED;
        if (!p.memsz) continue;
        if (p.vaddr < 4096 || p.vaddr >= USER_ADDRESS_END || p.memsz > USER_ADDRESS_END - p.vaddr) return ELF64_SEGMENT;
        if ((p.flags & ~7u) || !(p.flags & 4) || ((p.flags & 3) == 3)) return ELF64_PERMISSIONS;
        if ((p.align > 1 && ((p.align & (p.align - 1)) || ((p.vaddr - p.offset) & (p.align - 1)))) ||
                ((p.vaddr ^ p.offset) & 4095)) return ELF64_SEGMENT;
        uint64_t first = p.vaddr & ~4095ull, end = (p.vaddr + p.memsz + 4095) & ~4095ull;
        if (first < USER_STACK_TOP + 4096 && end > USER_STACK_BASE - 4096) return ELF64_OVERLAP;
        if (plan.count && first < plan.segments[plan.count - 1].end) return ELF64_OVERLAP;
        if (plan.count == ELF64_MAX_LOAD || (end - first) / 4096 > USER_SPACE_MAX_PAGES - 4 - plan.pages) return ELF64_LIMIT;
        plan.pages += (end - first) / 4096;
        plan.segments[plan.count++] = (struct elf64_segment){.offset = p.offset, .vaddr = p.vaddr,
            .filesz = p.filesz, .memsz = p.memsz, .first = first, .end = end,
            .permissions = ((p.flags & 2) ? VMM_WRITE : 0) | ((p.flags & 1) ? VMM_EXEC : 0)};
        if ((p.flags & 1) && h.entry >= p.vaddr && h.entry - p.vaddr < p.filesz) executable_entry = true;
    }
    if (!plan.count || !executable_entry) return ELF64_ENTRY;
    *out = plan; return ELF64_OK;
}
enum elf64_result elf64_build(struct address_space *space, const struct vmm_ops *ops, void *context,
        uint64_t root, uint64_t mask, const void *image, size_t bytes, const char *name, uint64_t *entry, uint64_t *stack) {
    if (entry) *entry = 0;
    if (stack) *stack = 0;
    if (!space || space->ready || !name || !entry || !stack) return ELF64_ARGUMENT;
    size_t length = 0;
    while (length <= ELF64_ARG0_MAX && name[length]) ++length;
    if (!length || length > ELF64_ARG0_MAX) return ELF64_ARGUMENT;
    struct elf64_plan plan; enum elf64_result result = elf64_validate(image, bytes, &plan);
    if (result != ELF64_OK) return result;
    if (!address_core_create(space, ops, context, root, mask)) return ELF64_MEMORY;
    for (unsigned int i = 0; i < plan.count; ++i) {
        const struct elf64_segment *s = &plan.segments[i];
        for (uint64_t va = s->first; va < s->end; va += 4096)
            if (!address_core_alloc(space, va, s->permissions)) goto rollback;
        if (!address_core_copy(space, s->vaddr, (void *)((const unsigned char *)image + s->offset), s->filesz, true)) goto rollback;
    }
    for (unsigned int i = 0; i < 4; ++i)
        if (!address_core_alloc(space, USER_STACK_BASE + i * 4096, VMM_WRITE)) goto rollback;
    uint64_t arg0 = (USER_STACK_TOP - length - 1) & ~15ull;
    uint64_t sp = arg0 - 10 * sizeof(uint64_t);
    uint64_t words[10] = {1, arg0, 0, 0, 6, 4096, 9, plan.entry, 0, 0};
    if (!address_core_copy(space, arg0, (void *)name, length + 1, true) ||
            !address_core_copy(space, sp, words, sizeof words, true)) goto rollback;
    *entry = plan.entry; *stack = sp; return ELF64_OK;
rollback:
    return address_core_destroy(space, root) ? ELF64_MEMORY : ELF64_STATE;
}
const char *elf64_error(enum elf64_result error) {
    static const char *const messages[] = {"OK", "argumento invalido", "cabecalho invalido", "formato/recurso nao suportado",
        "arquivo truncado", "segmento invalido", "permissoes invalidas", "paginas sobrepostas/reservadas",
        "limite excedido", "entry point invalido", "memoria insuficiente", "estado interno invalido", "erro de leitura VFS"};
    return (unsigned int)error < sizeof messages / sizeof *messages ? messages[error] : "erro desconhecido";
}
