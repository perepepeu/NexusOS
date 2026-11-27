#ifndef NEXUS_PROCESS_INTERNAL_H
#define NEXUS_PROCESS_INTERNAL_H
#include <process.h>
struct process_control_block { struct process_info info; bool used, system; };
struct thread_control_block {
    struct thread_info info;
    uint64_t sleep_start, sleep_ticks, waiting_for;
    uintptr_t context, stack;
    void (*entry)(void *);
    void *argument;
    bool system;
};
struct process_manager {
    struct process_control_block processes[PROCESS_MAX];
    struct thread_control_block tasks[THREAD_MAX];
    unsigned int current;
    uint64_t next_pid, next_tid;
    bool ready;
};
bool process_core_init(struct process_manager *s);
bool process_core_add_idle(struct process_manager *s);
bool process_core_has_ready(struct process_manager *s);
void process_core_input_wake(struct process_manager *s);
int process_core_find(struct process_manager *s, uint64_t pid);
int thread_core_find(struct process_manager *s, uint64_t tid);
int process_core_free_slot(struct process_manager *s);
int process_core_create(struct process_manager *s, const char *name);
int thread_core_create(struct process_manager *s, const char *name);
bool process_core_block(struct process_manager *s, enum process_block_reason reason, uint64_t value, uint64_t now);
void process_core_tick(struct process_manager *s, uint64_t now);
int process_core_pick(struct process_manager *s, uint64_t now);
bool process_core_finish(struct process_manager *s, uint64_t pid, int status);
bool thread_core_finish(struct process_manager *s, uint64_t tid, int status);
bool process_core_collect(struct process_manager *s, uint64_t child, int *status);
bool thread_core_collect(struct process_manager *s, uint64_t tid, int *status);
#endif
