#ifndef NEXUS_PROGRAMS_H
#define NEXUS_PROGRAMS_H
#include <limine.h>
#include <elf64.h>
enum { PROGRAMS_MAX = 8, PROGRAM_NAME_MAX = 23 };
struct boot_program {
    char name[PROGRAM_NAME_MAX + 1];
    const void *image; size_t bytes;
    enum elf64_result validation;
    uint64_t entry; unsigned int segments; size_t pages;
};
void programs_init(const struct limine_module_response *);
size_t programs_count(void);
const struct boot_program *programs_get(size_t index);
const struct boot_program *programs_find(const char *name);
bool programs_demo(int *alpha_status, int *beta_status);
void elf_process_selftest(void);
#endif
