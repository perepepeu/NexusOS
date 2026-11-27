#include <process_internal.h>
#include <assert.h>
#include <stdio.h>

static struct process_manager s;
static void running(unsigned int slot) {
    if (s.tasks[s.current].info.state == PROCESS_RUNNING) s.tasks[s.current].info.state = PROCESS_READY;
    s.current = slot; s.tasks[slot].info.state = PROCESS_RUNNING;
}
int main(void) {
    assert(process_core_find(&s, 1) < 0 && process_core_pick(&s, 0) < 0);
    assert(process_core_init(&s) && !process_core_init(&s));
    assert(!process_core_finish(&s, 1, 0) && !thread_core_finish(&s, 1, 0));
    assert(process_core_create(&s, 0) < 0 && thread_core_create(&s, "") < 0);
    int a = process_core_create(&s, "a"), b = process_core_create(&s, "b");
    uint64_t pa = s.tasks[a].info.pid, pb = s.tasks[b].info.pid;
    assert(pa == 2 && pb == 3 && a == 1 && b == 2);
    s.tasks[0].info.state = PROCESS_READY;
    assert(process_core_pick(&s, 0) == a);
    int t = thread_core_create(&s, "a-thread");
    assert(t == 3 && s.tasks[t].info.pid == pa && s.tasks[t].info.tid == 4);
    process_core_tick(&s, 1);
    assert(s.processes[process_core_find(&s, pa)].info.threads == 2);
    assert(s.processes[process_core_find(&s, pa)].info.cpu_ticks == 1);
    assert(!process_core_block(&s, PROCESS_BLOCK_SLEEP, 0, 0));
    assert(!process_core_block(&s, PROCESS_BLOCK_WAIT, pa, 0));
    assert(process_core_block(&s, PROCESS_BLOCK_SLEEP, 4, UINT64_MAX - 1));
    process_core_tick(&s, 0); assert(s.tasks[a].info.state == PROCESS_BLOCKED);
    process_core_tick(&s, 2); assert(s.tasks[a].info.state == PROCESS_READY);
    running((unsigned int)a);
    uint64_t tid = s.tasks[t].info.tid;
    assert(process_core_block(&s, PROCESS_BLOCK_JOIN, tid, 0));
    running((unsigned int)t);
    assert(!process_core_block(&s, PROCESS_BLOCK_JOIN, s.tasks[a].info.tid, 0)); /* Ciclo. */
    assert(thread_core_finish(&s, tid, 17) && s.tasks[a].info.state == PROCESS_READY);
    running((unsigned int)a);
    int status;
    s.tasks[t].stack = 4096; assert(!thread_core_collect(&s, tid, 0));
    s.tasks[t].stack = 0;
    assert(thread_core_collect(&s, tid, &status) && status == 17);
    assert(s.processes[process_core_find(&s, pa)].info.threads == 1);
    t = thread_core_create(&s, "survivor");
    assert(thread_core_finish(&s, s.tasks[a].info.tid, 0));
    assert(s.processes[process_core_find(&s, pa)].info.state != PROCESS_TERMINATED);
    assert(thread_core_finish(&s, s.tasks[t].info.tid, 23));
    assert(s.processes[process_core_find(&s, pa)].info.state == PROCESS_TERMINATED);
    running(0);
    assert(process_core_collect(&s, pa, &status) && status == 23);
    assert(!process_core_collect(&s, pa, 0) && process_core_find(&s, pa) < 0);
    running((unsigned int)b);
    int child_thread = process_core_create(&s, "orphan");
    uint64_t child = s.tasks[child_thread].info.pid;
    assert(s.processes[process_core_find(&s, child)].info.parent == pb);
    int sibling = thread_core_create(&s, "kill-group");
    running(0);
    assert(process_core_block(&s, PROCESS_BLOCK_WAIT, pb, 0));
    assert(process_core_finish(&s, pb, -9) && s.tasks[0].info.state == PROCESS_READY);
    assert(s.tasks[sibling].info.state == PROCESS_TERMINATED);
    assert(s.processes[process_core_find(&s, child)].info.parent == 1);
    running(0); assert(process_core_collect(&s, pb, &status) && status == -9);
    assert(process_core_finish(&s, child, 0) && process_core_collect(&s, child, 0));
    uint64_t old_pid = s.next_pid, old_tid = s.next_tid;
    for (unsigned int i = 0; i < 1000; ++i) {
        int slot = process_core_create(&s, "cycle"); assert(slot > 0);
        assert(s.tasks[slot].info.pid == old_pid++ && s.tasks[slot].info.tid == old_tid++);
        assert(process_core_finish(&s, s.tasks[slot].info.pid, 0));
        assert(process_core_collect(&s, old_pid - 1, 0));
    }
    for (unsigned int i = 1; i < PROCESS_MAX; ++i) assert(process_core_create(&s, "pcb-full") > 0);
    assert(process_core_create(&s, "no-pcb") < 0);
    for (unsigned int i = 1; i < PROCESS_MAX; ++i) {
        uint64_t pid = s.processes[i].info.pid;
        assert(process_core_finish(&s, pid, 0) && process_core_collect(&s, pid, 0));
    }
    for (unsigned int i = 1; i < THREAD_MAX; ++i) assert(thread_core_create(&s, "tcb-full") > 0);
    assert(thread_core_create(&s, "no-tcb") < 0 && process_core_create(&s, "no-tcb") < 0);
    for (unsigned int i = 1; i < THREAD_MAX; ++i) {
        uint64_t id = s.tasks[i].info.tid;
        assert(thread_core_finish(&s, id, 0) && thread_core_collect(&s, id, 0));
    }
    assert(!process_core_block(&s, PROCESS_BLOCK_JOIN, 99999, 0));
    s.next_pid = UINT64_MAX; assert(process_core_create(&s, "pid-wrap") < 0);
    s.next_tid = UINT64_MAX; assert(thread_core_create(&s, "tid-wrap") < 0);
    s = (struct process_manager){0}; assert(process_core_init(&s) && process_core_add_idle(&s));
    assert(!process_core_add_idle(&s));
    assert(process_core_find(&s, 0) == 1 && thread_core_find(&s, 0) == 1);
    s.tasks[0].info.state = PROCESS_READY;
    assert(process_core_pick(&s, 0) == 0); /* Idle nao compete com trabalho pronto. */
    assert(process_core_block(&s, PROCESS_BLOCK_SLEEP, 2, 0));
    assert(process_core_pick(&s, 0) == 1);
    assert(!process_core_has_ready(&s) && !process_core_finish(&s, 0, -9) && !thread_core_finish(&s, 0, -9));
    assert(process_core_create(&s, "from-idle") < 0 && thread_core_create(&s, "from-idle") < 0);
    assert(!process_core_block(&s, PROCESS_BLOCK_SLEEP, 1, 0));
    process_core_tick(&s, 2); s.tasks[1].info.state = PROCESS_READY;
    assert(process_core_has_ready(&s) && process_core_pick(&s, 2) == 0);
    assert(process_core_block(&s, PROCESS_BLOCK_INPUT, 0, 2));
    assert(process_core_pick(&s, 2) == 1);
    process_core_tick(&s, 3); assert(s.tasks[0].info.state == PROCESS_BLOCKED);
    process_core_input_wake(&s); assert(s.tasks[0].info.state == PROCESS_READY);
    s.tasks[1].info.state = PROCESS_READY; assert(process_core_pick(&s, 3) == 0);
    assert(!process_core_collect(&s, 0, 0) && !thread_core_collect(&s, 0, 0));
    puts("OK: PCB/TCB, PID/TID, estados, join/ciclos, grupos, orfaos, limites e reutilizacao.");
}
