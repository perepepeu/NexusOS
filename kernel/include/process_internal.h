#ifndef NEXUS_PROCESS_INTERNAL_H
#define NEXUS_PROCESS_INTERNAL_H
#include <process.h>
struct process_record {
    struct process_info info;
    uint64_t sleep_start, sleep_ticks, waiting_for;
    uintptr_t context, stack;
    void (*entry)(void *);
    void *argument;
};
struct process_manager { struct process_record tasks[PROCESS_MAX]; unsigned int current; uint64_t next_pid; bool ready; };
bool process_core_init(struct process_manager *s);
int process_core_find(struct process_manager *s, uint64_t pid);
int process_core_free_slot(struct process_manager *s);
int process_core_create(struct process_manager *s, const char *name);
void process_core_tick(struct process_manager *s, uint64_t now);
int process_core_pick(struct process_manager *s, uint64_t now);
bool process_core_finish(struct process_manager *s, uint64_t pid, int status);
bool process_core_collect(struct process_manager *s, uint64_t child, int *status);
#endif
