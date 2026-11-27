#ifndef NEXUS_PROCESS_H
#define NEXUS_PROCESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PROCESS_MAX = 32, THREAD_MAX = 64, PROCESS_NAME = 24 };
enum process_state { PROCESS_UNUSED, PROCESS_READY, PROCESS_RUNNING, PROCESS_BLOCKED, PROCESS_TERMINATED };
enum process_block_reason { PROCESS_NOT_BLOCKED, PROCESS_BLOCK_SLEEP, PROCESS_BLOCK_WAIT, PROCESS_BLOCK_JOIN, PROCESS_BLOCK_MIXED };
struct process_info {
    uint64_t pid, parent, cpu_ticks;
    size_t threads;
    size_t resources;
    enum process_state state;
    enum process_block_reason blocked_on;
    int exit_status;
    char name[PROCESS_NAME];
};
struct thread_info {
    uint64_t tid, pid, cpu_ticks;
    enum process_state state;
    enum process_block_reason blocked_on;
    int exit_status;
    char name[PROCESS_NAME];
};
/* Processos ring 0 cooperativos; API fora de IRQ/NMI, em uma CPU.
 * Entry/argumento devem continuar validos ate a execucao terminar. */
bool process_init(void);
uint64_t process_create(const char *name, void (*entry)(void *), void *argument);
uint64_t process_current_pid(void);
uint64_t thread_current_tid(void);
uint64_t thread_create(const char *name, void (*entry)(void *), void *argument);
bool thread_join(uint64_t tid, int *status);
__attribute__((noreturn)) void thread_exit(int status);
void process_yield(void);
void process_sleep(uint64_t ticks);
bool process_kill(uint64_t pid, int status);
bool process_wait(uint64_t child, int *status);
__attribute__((noreturn)) void process_exit(int status);
size_t process_snapshot(struct process_info *out, size_t capacity);
size_t thread_snapshot(struct thread_info *out, size_t capacity);
const char *process_state_name(enum process_state state);
void process_on_tick(uint64_t now);
void process_selftest(bool guard_fault);
#endif
