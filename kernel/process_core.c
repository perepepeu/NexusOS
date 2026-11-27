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
bool process_core_block(struct process_manager *s, enum process_block_reason reason, uint64_t value, uint64_t now) {
    if (!s || !s->ready || s->tasks[s->current].info.state != PROCESS_RUNNING) return false;
    struct process_record *p = &s->tasks[s->current];
    if (reason == PROCESS_BLOCK_SLEEP) {
        if (!value || value > INT64_MAX) return false;
        p->sleep_start = now; p->sleep_ticks = value;
    } else if (reason == PROCESS_BLOCK_WAIT) {
        int child = process_core_find(s, value);
        if (child <= 0 || (unsigned int)child == s->current || s->tasks[child].info.parent != p->info.pid ||
                s->tasks[child].info.state == PROCESS_TERMINATED) return false;
        p->waiting_for = value;
    } else return false;
    p->info.state = PROCESS_BLOCKED; p->info.blocked_on = reason; return true;
}
static void wake_sleepers(struct process_manager *s, uint64_t now) {
    for (unsigned int i = 0; i < PROCESS_MAX; ++i) {
        struct process_record *p = &s->tasks[i];
        if (p->info.state == PROCESS_BLOCKED && p->info.blocked_on == PROCESS_BLOCK_SLEEP && now - p->sleep_start >= p->sleep_ticks) {
            p->info.state = PROCESS_READY; p->info.blocked_on = PROCESS_NOT_BLOCKED;
        }
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
    if (slot <= 0 || s->tasks[slot].info.state == PROCESS_TERMINATED) return false;
    struct process_record *p = &s->tasks[slot];
    p->info.state = PROCESS_TERMINATED; p->info.blocked_on = PROCESS_NOT_BLOCKED; p->info.exit_status = status;
    for (unsigned int i = 0; i < PROCESS_MAX; ++i) {
        struct process_record *other = &s->tasks[i];
        if (other->info.parent == pid) other->info.parent = 1;
        if (other->info.pid == p->info.parent && other->info.state == PROCESS_BLOCKED &&
                other->info.blocked_on == PROCESS_BLOCK_WAIT && other->waiting_for == pid) {
            other->info.state = PROCESS_READY; other->info.blocked_on = PROCESS_NOT_BLOCKED;
        }
    }
    return true;
}
bool process_core_collect(struct process_manager *s, uint64_t child, int *status) {
    int slot = process_core_find(s, child);
    if (slot <= 0 || (unsigned int)slot == s->current) return false;
    struct process_record *p = &s->tasks[slot];
    if (p->info.parent != s->tasks[s->current].info.pid || p->info.state != PROCESS_TERMINATED || p->stack) return false;
    if (status) *status = p->info.exit_status;
    *p = (struct process_record){0}; return true;
}
const char *process_state_name(enum process_state state) {
    static const char *const names[] = {"UNUSED", "READY", "RUNNING", "BLOCKED", "TERMINATED"};
    return (unsigned int)state < sizeof names / sizeof names[0] ? names[state] : "INVALID";
}
