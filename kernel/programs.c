#include <programs.h>
#include <process.h>
#include <log.h>
static struct boot_program catalog[PROGRAMS_MAX];
static size_t count;
static bool equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
const struct boot_program *programs_find(const char *name) {
    if (!name) return 0;
    for (size_t i = 0; i < count; ++i) if (equal(catalog[i].name, name)) return &catalog[i];
    return 0;
}
size_t programs_count(void) { return count; }
const struct boot_program *programs_get(size_t index) { return index < count ? &catalog[index] : 0; }
void programs_init(const struct limine_module_response *response) {
    count = 0;
    if (!response || !response->modules) { log_warn("elf", "Nenhum modulo Limine disponivel."); return; }
    for (uint64_t i = 0; i < response->module_count && i < 32 && count < PROGRAMS_MAX; ++i) {
        const struct limine_file *file = response->modules[i];
        if (!file || !file->address || !file->string) continue;
        size_t length = 0;
        while (length <= PROGRAM_NAME_MAX && file->string[length]) {
            char c = file->string[length];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) break;
            ++length;
        }
        if (!length || length > PROGRAM_NAME_MAX || file->string[length] || programs_find(file->string)) {
            log_warn("elf", "Modulo ignorado: nome ausente, invalido ou duplicado."); continue;
        }
        struct boot_program *p = &catalog[count];
        *p = (struct boot_program){.image = file->address, .bytes = file->size};
        for (size_t j = 0; j <= length; ++j) p->name[j] = file->string[j];
        struct elf64_plan plan;
        p->validation = elf64_validate(p->image, p->bytes, &plan);
        if (p->validation == ELF64_OK) {
            p->entry = plan.entry; p->segments = plan.count; p->pages = plan.pages;
            log_info("elf", "Modulo pronto: %s ELF64 entry=%p segmentos=%u", p->name, (void *)(uintptr_t)p->entry, p->segments);
        } else log_warn("elf", "Modulo %s recusado: %s", p->name, elf64_error(p->validation));
        ++count;
    }
}
bool programs_demo(int *alpha_status, int *beta_status) {
    const struct boot_program *a = programs_find("alpha"), *b = programs_find("beta");
    if (!a || !b || !alpha_status || !beta_status) return false;
    process_preempt_disable();
    uint64_t pa = process_create_elf(a->name, a->image, a->bytes, 0);
    uint64_t pb = pa ? process_create_elf(b->name, b->image, b->bytes, 0) : 0;
    process_preempt_enable();
    if (!pa || !pb) {
        int status;
        if (pa) { (void)process_kill(pa, -9); (void)process_wait(pa, &status); }
        return false;
    }
    bool collected_a = process_wait(pa, alpha_status);
    bool collected_b = process_wait(pb, beta_status);
    return collected_a && collected_b;
}
