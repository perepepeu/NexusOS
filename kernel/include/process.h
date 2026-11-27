#ifndef NEXUS_PROCESS_H
#define NEXUS_PROCESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PROCESS_MAX = 32, PROCESS_NAME = 24 };
enum process_state { PROCESS_UNUSED, PROCESS_READY, PROCESS_RUNNING, PROCESS_SLEEPING, PROCESS_WAITING, PROCESS_ZOMBIE };
struct process_info {
    uint64_t pid, parent, cpu_ticks;
    enum process_state state;
    int exit_status;
    char name[PROCESS_NAME];
};
/* Processos ring 0 cooperativos; API fora de IRQ/NMI, em uma CPU.
 * Entry/argumento devem continuar validos ate a execucao terminar. */
bool process_init(void);
uint64_t process_create(const char *name, void (*entry)(void *), void *argument);
uint64_t process_current_pid(void);
void process_yield(void);
void process_sleep(uint64_t ticks);
bool process_kill(uint64_t pid, int status);
bool process_wait(uint64_t child, int *status);
__attribute__((noreturn)) void process_exit(int status);
size_t process_snapshot(struct process_info *out, size_t capacity);
const char *process_state_name(enum process_state state);
void process_on_tick(uint64_t now);
void process_selftest(bool guard_fault);
#endif
