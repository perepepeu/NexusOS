#include <monitor.h>
static unsigned int cpu_permille(uint64_t ticks, uint64_t elapsed) {
    if (!elapsed) return 0;
    if (ticks >= elapsed) return 1000;
    /* Comparar contra ceil(elapsed*p/1000), sem multiplicacao de 64 bits
     * que possa estourar e sem divisao de 128 bits/libgcc no kernel. */
    unsigned int low = 0, high = 1000;
    while (low < high) {
        unsigned int mid = (low + high + 1) / 2;
        uint64_t threshold = (elapsed / 1000) * mid + ((elapsed % 1000) * mid + 999) / 1000;
        if (ticks >= threshold) low = mid; else high = mid - 1;
    }
    return low;
}
size_t monitor_build(const struct process_info *before, size_t n_before,
        const struct process_info *after, size_t n_after, uint64_t elapsed,
        struct monitor_row *out, size_t capacity) {
    if ((!before && n_before) || (!after && n_after) || !out || n_before > PROCESS_MAX || n_after > PROCESS_MAX) return 0;
    size_t count = n_after < capacity ? n_after : capacity;
    for (size_t i = 0; i < count; ++i) {
        uint64_t ticks = after[i].cpu_ticks;
        for (size_t j = 0; j < n_before; ++j) if (before[j].pid == after[i].pid) { ticks -= before[j].cpu_ticks; break; }
        if (ticks > elapsed) ticks = elapsed;
        struct monitor_row row = {.process = after[i], .ticks = ticks, .permille = cpu_permille(ticks, elapsed)};
        size_t position = i;
        while (position && (out[position - 1].ticks < row.ticks ||
                (out[position - 1].ticks == row.ticks && out[position - 1].process.pid > row.process.pid))) {
            out[position] = out[position - 1]; --position;
        }
        out[position] = row;
    }
    return count;
}
