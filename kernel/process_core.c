#include <process_internal.h>

static void name_copy(char *out, const char *name) {
    for (size_t i = 0; i < PROCESS_NAME - 1 && name[i]; ++i) out[i] = name[i];
}
bool process_core_init(struct process_manager *s) {
    if (!s || s->ready) return false;
    *s = (struct process_manager){.ready = true, .next_pid = 2, .next_tid = 2};
    s->processes[0] = (struct process_control_block){.used = true, .system = true,
        .info = {.pid = 1, .threads = 1, .state = PROCESS_RUNNING, .name = "kernel"}};
    s->tasks[0] = (struct thread_control_block){.system = true,
        .info = {.tid = 1, .pid = 1, .state = PROCESS_RUNNING, .name = "kernel-main"}};
    return true;
}
bool process_core_add_idle(struct process_manager *s) {
    if (!s || !s->ready || s->processes[1].used || s->tasks[1].info.state != PROCESS_UNUSED) return false;
    s->processes[1] = (struct process_control_block){.used = true, .system = true,
        .info = {.pid = 0, .threads = 1, .state = PROCESS_READY, .name = "idle"}};
    s->tasks[1] = (struct thread_control_block){.system = true,
        .info = {.tid = 0, .pid = 0, .state = PROCESS_READY, .name = "idle"}};
    return true;
}
int process_core_find(struct process_manager *s, uint64_t pid) {
    if (!s || !s->ready) return -1;
    for (unsigned int i = 0; i < PROCESS_MAX; ++i)
        if (s->processes[i].used && s->processes[i].info.pid == pid) return (int)i;
    return -1;
}
int thread_core_find(struct process_manager *s, uint64_t tid) {
    if (!s || !s->ready) return -1;
    for (unsigned int i = 0; i < THREAD_MAX; ++i)
        if (s->tasks[i].info.state != PROCESS_UNUSED && s->tasks[i].info.tid == tid) return (int)i;
    return -1;
}
static void sync_states(struct process_manager *s) {
    for (unsigned int i = 0; i < PROCESS_MAX; ++i) {
        struct process_info *p = &s->processes[i].info;
        if (!s->processes[i].used || p->state == PROCESS_TERMINATED) continue;
        size_t running = 0, ready = 0, live = 0;
        enum process_block_reason reason = PROCESS_NOT_BLOCKED;
        for (unsigned int j = 0; j < THREAD_MAX; ++j) {
            struct thread_info *t = &s->tasks[j].info;
            if (t->state == PROCESS_UNUSED || t->state == PROCESS_TERMINATED || t->pid != p->pid) continue;
            ++live; running += t->state == PROCESS_RUNNING; ready += t->state == PROCESS_READY;
            if (t->state == PROCESS_BLOCKED)
                reason = reason == PROCESS_NOT_BLOCKED ? t->blocked_on : reason == t->blocked_on ? reason : PROCESS_BLOCK_MIXED;
        }
        p->threads = live;
        p->state = running ? PROCESS_RUNNING : ready ? PROCESS_READY : PROCESS_BLOCKED;
        p->blocked_on = running || ready ? PROCESS_NOT_BLOCKED : reason;
    }
}
int process_core_free_slot(struct process_manager *s) {
    if (!s || !s->ready || s->next_tid == UINT64_MAX) return -1;
    for (unsigned int i = 1; i < THREAD_MAX; ++i)
        if (s->tasks[i].info.state == PROCESS_UNUSED) return (int)i;
    return -1;
}
static int new_thread(struct process_manager *s, const char *name, uint64_t pid) {
    int slot = process_core_free_slot(s);
    if (slot < 0) return -1;
    s->tasks[slot] = (struct thread_control_block){.info = {
        .pid = pid, .tid = s->next_tid++, .state = PROCESS_READY}};
    name_copy(s->tasks[slot].info.name, name);
    return slot;
}
int process_core_create(struct process_manager *s, const char *name) {
    if (process_core_free_slot(s) < 0 || !name || !*name || s->next_pid == UINT64_MAX ||
            s->tasks[s->current].info.state != PROCESS_RUNNING || !s->tasks[s->current].info.pid) return -1;
    int pcb = -1;
    for (unsigned int i = 1; i < PROCESS_MAX; ++i) if (!s->processes[i].used) { pcb = (int)i; break; }
    if (pcb < 0) return -1;
    uint64_t pid = s->next_pid++;
    s->processes[pcb] = (struct process_control_block){.used = true, .info = {
        .pid = pid, .parent = s->tasks[s->current].info.pid, .state = PROCESS_READY}};
    name_copy(s->processes[pcb].info.name, name);
    int slot = new_thread(s, name, pid); sync_states(s); return slot;
}
int thread_core_create(struct process_manager *s, const char *name) {
    if (!s || !s->ready || !name || !*name || s->tasks[s->current].info.state != PROCESS_RUNNING || !s->tasks[s->current].info.pid) return -1;
    int slot = new_thread(s, name, s->tasks[s->current].info.pid);
    sync_states(s); return slot;
}
bool process_core_block(struct process_manager *s, enum process_block_reason reason, uint64_t value, uint64_t now) {
    if (!s || !s->ready || s->tasks[s->current].info.state != PROCESS_RUNNING) return false;
    struct thread_control_block *t = &s->tasks[s->current];
    if (!t->info.pid) return false;
    if (reason == PROCESS_BLOCK_SLEEP) {
        if (!value || value > INT64_MAX) return false;
        t->sleep_start = now; t->sleep_ticks = value;
    } else if (reason == PROCESS_BLOCK_WAIT) {
        int pcb = process_core_find(s, value);
        if (pcb < 0 || s->processes[pcb].system || value == t->info.pid ||
                s->processes[pcb].info.parent != t->info.pid || s->processes[pcb].info.state == PROCESS_TERMINATED) return false;
        t->waiting_for = value;
    } else if (reason == PROCESS_BLOCK_JOIN) {
        int slot = thread_core_find(s, value);
        if (slot < 0 || (unsigned int)slot == s->current || s->tasks[slot].system ||
                s->tasks[slot].info.pid != t->info.pid || s->tasks[slot].info.state == PROCESS_TERMINATED) return false;
        /* Recusa ciclos de join antes de bloquear a thread atual. */
        for (unsigned int n = 0; n < THREAD_MAX; ++n) {
            struct thread_control_block *target = &s->tasks[slot];
            if (target->info.tid == t->info.tid) return false;
            if (target->info.state != PROCESS_BLOCKED || target->info.blocked_on != PROCESS_BLOCK_JOIN) break;
            slot = thread_core_find(s, target->waiting_for);
            if (slot < 0) break;
            if (n == THREAD_MAX - 1) return false;
        }
        t->waiting_for = value;
    } else if (reason != PROCESS_BLOCK_INPUT) return false;
    t->info.state = PROCESS_BLOCKED; t->info.blocked_on = reason; sync_states(s); return true;
}
static void wake_sleepers(struct process_manager *s, uint64_t now) {
    for (unsigned int i = 0; i < THREAD_MAX; ++i) {
        struct thread_control_block *t = &s->tasks[i];
        if (t->info.state == PROCESS_BLOCKED && t->info.blocked_on == PROCESS_BLOCK_SLEEP && now - t->sleep_start >= t->sleep_ticks) {
            t->info.state = PROCESS_READY; t->info.blocked_on = PROCESS_NOT_BLOCKED;
        }
    }
}
void process_core_tick(struct process_manager *s, uint64_t now) {
    if (!s || !s->ready) return;
    struct thread_info *t = &s->tasks[s->current].info;
    if (t->state == PROCESS_RUNNING) {
        ++t->cpu_ticks;
        int pcb = process_core_find(s, t->pid);
        if (pcb >= 0) ++s->processes[pcb].info.cpu_ticks;
    }
    wake_sleepers(s, now); sync_states(s);
}
int process_core_pick(struct process_manager *s, uint64_t now) {
    if (!s || !s->ready) return -1;
    wake_sleepers(s, now);
    for (unsigned int n = 1; n <= THREAD_MAX; ++n) {
        unsigned int i = (s->current + n) % THREAD_MAX;
        if (s->tasks[i].info.state == PROCESS_READY && s->tasks[i].info.pid != 0) {
            s->current = i; s->tasks[i].info.state = PROCESS_RUNNING; sync_states(s); return (int)i;
        }
    }
    int idle = thread_core_find(s, 0);
    if (idle >= 0 && s->tasks[idle].info.state == PROCESS_READY) {
        s->current = (unsigned int)idle; s->tasks[idle].info.state = PROCESS_RUNNING;
        sync_states(s); return idle;
    }
    sync_states(s); return -1;
}
bool process_core_has_ready(struct process_manager *s) {
    if (!s || !s->ready) return false;
    for (unsigned int i = 0; i < THREAD_MAX; ++i)
        if (s->tasks[i].info.pid && s->tasks[i].info.state == PROCESS_READY) return true;
    return false;
}
void process_core_input_wake(struct process_manager *s) {
    if (!s || !s->ready) return;
    for (unsigned int i = 0; i < THREAD_MAX; ++i) {
        struct thread_info *t = &s->tasks[i].info;
        if (t->state == PROCESS_BLOCKED && t->blocked_on == PROCESS_BLOCK_INPUT) {
            t->state = PROCESS_READY; t->blocked_on = PROCESS_NOT_BLOCKED;
        }
    }
    sync_states(s);
}
bool process_core_finish(struct process_manager *s, uint64_t pid, int status) {
    int pcb = process_core_find(s, pid);
    if (pcb < 0 || s->processes[pcb].system || s->processes[pcb].info.state == PROCESS_TERMINATED) return false;
    struct process_info *p = &s->processes[pcb].info;
    p->state = PROCESS_TERMINATED; p->blocked_on = PROCESS_NOT_BLOCKED; p->exit_status = status; p->threads = 0;
    for (unsigned int i = 0; i < PROCESS_MAX; ++i)
        if (s->processes[i].used && s->processes[i].info.parent == pid) s->processes[i].info.parent = 1;
    for (unsigned int i = 0; i < THREAD_MAX; ++i) {
        struct thread_control_block *t = &s->tasks[i];
        if (t->info.state == PROCESS_UNUSED) continue;
        if (t->info.pid == pid) {
            t->info.state = PROCESS_TERMINATED; t->info.blocked_on = PROCESS_NOT_BLOCKED; t->info.exit_status = status;
        } else if (t->info.state == PROCESS_BLOCKED && t->info.blocked_on == PROCESS_BLOCK_WAIT && t->waiting_for == pid) {
            t->info.state = PROCESS_READY; t->info.blocked_on = PROCESS_NOT_BLOCKED;
        }
    }
    sync_states(s); return true;
}
bool thread_core_finish(struct process_manager *s, uint64_t tid, int status) {
    int slot = thread_core_find(s, tid);
    if (slot < 0 || s->tasks[slot].system || s->tasks[slot].info.state == PROCESS_TERMINATED) return false;
    struct thread_info *t = &s->tasks[slot].info;
    t->state = PROCESS_TERMINATED; t->blocked_on = PROCESS_NOT_BLOCKED; t->exit_status = status;
    bool alive = false;
    for (unsigned int i = 0; i < THREAD_MAX; ++i) {
        struct thread_control_block *other = &s->tasks[i];
        if (other->info.state == PROCESS_UNUSED || other->info.pid != t->pid) continue;
        if (other->info.state != PROCESS_TERMINATED) alive = true;
        if (other->info.state == PROCESS_BLOCKED && other->info.blocked_on == PROCESS_BLOCK_JOIN && other->waiting_for == tid) {
            other->info.state = PROCESS_READY; other->info.blocked_on = PROCESS_NOT_BLOCKED;
        }
    }
    if (!alive) return process_core_finish(s, t->pid, status);
    sync_states(s); return true;
}
bool process_core_collect(struct process_manager *s, uint64_t child, int *status) {
    int pcb = process_core_find(s, child);
    if (pcb < 0 || s->processes[pcb].system) return false;
    struct process_info *p = &s->processes[pcb].info;
    if (p->parent != s->tasks[s->current].info.pid || p->state != PROCESS_TERMINATED) return false;
    for (unsigned int i = 0; i < THREAD_MAX; ++i) if (s->tasks[i].info.state != PROCESS_UNUSED &&
            s->tasks[i].info.pid == child && s->tasks[i].stack) return false;
    if (status) *status = p->exit_status;
    for (unsigned int i = 0; i < THREAD_MAX; ++i)
        if (s->tasks[i].info.state != PROCESS_UNUSED && s->tasks[i].info.pid == child) s->tasks[i] = (struct thread_control_block){0};
    s->processes[pcb] = (struct process_control_block){0}; return true;
}
bool thread_core_collect(struct process_manager *s, uint64_t tid, int *status) {
    int slot = thread_core_find(s, tid);
    if (slot < 0 || (unsigned int)slot == s->current || s->tasks[slot].system) return false;
    struct thread_control_block *t = &s->tasks[slot];
    if (t->info.pid != s->tasks[s->current].info.pid || t->info.state != PROCESS_TERMINATED || t->stack) return false;
    if (status) *status = t->info.exit_status;
    *t = (struct thread_control_block){0}; sync_states(s); return true;
}
const char *process_state_name(enum process_state state) {
    static const char *const names[] = {"UNUSED", "READY", "RUNNING", "BLOCKED", "TERMINATED"};
    return (unsigned int)state < sizeof names / sizeof names[0] ? names[state] : "INVALID";
}
