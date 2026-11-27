#ifndef NEXUS_HEAP_H
#define NEXUS_HEAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct heap_stats { size_t mapped_bytes, requested_bytes, allocations, arenas; bool ready; };
bool heap_init(void);
/* Alinhamento de 16 bytes. Zero/overflow/OOM retornam NULL. Uso fora de IRQ/NMI.
 * kfree(NULL) e valido; ponteiros invalidos/corrupcao detectada causam panic.
 * Guardas por arena e redzones verificadas nas operacoes e em heap_check.
 * Nao instrumenta leituras/escritas individuais em paginas mapeadas. Sem SMP. */
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *pointer, size_t size);
void kfree(void *pointer);
void heap_get_stats(struct heap_stats *out);
void heap_selftest(void);
void heap_check(void);
const char *heap_address_kind(uint64_t address);
void heap_access_selftest(unsigned int scenario);
#endif
