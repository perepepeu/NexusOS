#ifndef NEXUS_PROCESS_RESOURCES_H
#define NEXUS_PROCESS_RESOURCES_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/* Uso normal com IRQs habilitadas. Recursos pertencem ao PID, nao ao TID. */
uint64_t process_alloc_pages(uint64_t count, uint64_t alignment);
void *process_map_pages(size_t count, unsigned int flags);
bool process_resource_attach(void *resource, void (*release)(void *));
bool process_resource_release(void *resource);
bool process_resource_transfer(void *resource, uint64_t owner);
size_t process_resource_count(uint64_t owner);
/* Integracao interna: estas rotinas sao chamadas com IRQs desabilitadas. */
int process_heap_prepare(void *pointer);
void process_heap_finish(int token, void *old, void *pointer, size_t size);
void process_heap_forget(void *pointer);
void process_resources_reap(uint64_t pid);
bool process_owner_alive(uint64_t pid);
#endif
