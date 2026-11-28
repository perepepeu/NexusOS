#ifndef NEXUS_MONITOR_H
#define NEXUS_MONITOR_H
#include <process.h>
struct monitor_row { struct process_info process; uint64_t ticks; unsigned int permille; };
/* Janela comum de ticks do PIT, por PID (nao por slot); ordena CPU decrescente. */
size_t monitor_build(const struct process_info *before, size_t n_before,
    const struct process_info *after, size_t n_after, uint64_t elapsed,
    struct monitor_row *out, size_t capacity);
#endif
