#include <process_internal.h>

bool process_core_init(struct process_manager *s) {
    if (!s || s->ready) return false;
    *s = (struct process_manager){.ready = true, .next_pid = 2};
    s->tasks[0].info = (struct process_info){.pid = 1, .state = PROCESS_RUNNING, .name = "kernel"};
    return true;
}
int process_core_find(struct process_manager *s, uint64_t pid) {
    if (!s || !s->ready || !pid) return -1;
    for (unsigned int i = 0; i < PROCESS_MAX; ++i)
        if (s->tasks[i].info.state != PROCESS_UNUSED && s->tasks[i].info.pid == pid) return (int)i;
    return -1;
}
int process_core_free_slot(struct process_manager *s) {
    if (!s || !s->ready || s->next_pid == UINT64_MAX) return -1;
    for (unsigned int i = 1; i < PROCESS_MAX; ++i)
        if (s->tasks[i].info.state == PROCESS_UNUSED) return (int)i;
    return -1;
}
int process_core_create(struct process_manager *s, const char *name) {
    int slot = process_core_free_slot(s);
    if (slot < 0 || !name || !*name || s->tasks[s->current].info.state != PROCESS_RUNNING) return -1;
    struct process_record *p = &s->tasks[slot];
    *p = (struct process_record){.info = {.pid = s->next_pid++, .parent = s->tasks[s->current].info.pid,
        .state = PROCESS_READY}};
    for (size_t i = 0; i < PROCESS_NAME - 1 && name[i]; ++i) p->info.name[i] = name[i];
    return slot;
}
static void wake_sleepers(struct process_manager *s, uint64_t now) {
    for (unsigned int i = 0; i < PROCESS_MAX; ++i) {
        struct process_record *p = &s->tasks[i];
        if (p->info.state == PROCESS_SLEEPING && now - p->sleep_start >= p->sleep_ticks)
            p->info.state = PROCESS_READY;
    }
}
void process_core_tick(struct process_manager *s, uint64_t now) {
    if (!s || !s->ready) return;
    if (s->tasks[s->current].info.state == PROCESS_RUNNING) ++s->tasks[s->current].info.cpu_ticks;
    wake_sleepers(s, now);
}
int process_core_pick(struct process_manager *s, uint64_t now) {
    if (!s || !s->ready) return -1;
    wake_sleepers(s, now);
    for (unsigned int n = 1; n <= PROCESS_MAX; ++n) {
        unsigned int i = (s->current + n) % PROCESS_MAX;
        if (s->tasks[i].info.state == PROCESS_READY) {
            s->current = i; s->tasks[i].info.state = PROCESS_RUNNING; return (int)i;
        }
    }
    return -1;
}
bool process_core_finish(struct process_manager *s, uint64_t pid, int status) {
    int slot = process_core_find(s, pid);
    if (slot <= 0 || s->tasks[slot].info.state == PROCESS_ZOMBIE) return false;
    struct process_record *p = &s->tasks[slot];
    p->info.state = PROCESS_ZOMBIE; p->info.exit_status = status;
    for (unsigned int i = 0; i < PROCESS_MAX; ++i) {
        struct process_record *other = &s->tasks[i];
        if (other->info.parent == pid) other->info.parent = 1;
        if (other->info.pid == p->info.parent && other->info.state == PROCESS_WAITING && other->waiting_for == pid)
            other->info.state = PROCESS_READY;
    }
    return true;
}
bool process_core_collect(struct process_manager *s, uint64_t child, int *status) {
    int slot = process_core_find(s, child);
    if (slot <= 0 || (unsigned int)slot == s->current) return false;
    struct process_record *p = &s->tasks[slot];
    if (p->info.parent != s->tasks[s->current].info.pid || p->info.state != PROCESS_ZOMBIE || p->stack) return false;
    if (status) *status = p->info.exit_status;
    *p = (struct process_record){0}; return true;
}
const char *process_state_name(enum process_state state) {
    static const char *const names[] = {"UNUSED", "READY", "RUNNING", "SLEEPING", "WAITING", "ZOMBIE"};
    return (unsigned int)state < sizeof names / sizeof names[0] ? names[state] : "INVALID";
}
