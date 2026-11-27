#include <process_internal.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct process_manager s = {0};
    assert(process_core_find(&s, 1) < 0 && process_core_pick(&s, 0) < 0);
    assert(process_core_init(&s) && !process_core_init(&s));
    assert(process_core_find(&s, 1) == 0 && !process_core_finish(&s, 1, 0));
    assert(process_core_create(&s, 0) < 0 && process_core_create(&s, "") < 0);
    int a = process_core_create(&s, "a"), b = process_core_create(&s, "b");
    assert(a == 1 && b == 2 && s.tasks[a].info.pid == 2 && s.tasks[b].info.pid == 3);
    assert(s.tasks[a].info.parent == 1 && s.tasks[b].info.parent == 1);
    s.tasks[0].info.state = PROCESS_READY;
    assert(process_core_pick(&s, 0) == a);
    process_core_tick(&s, 1); assert(s.tasks[a].info.cpu_ticks == 1);
    s.tasks[a].info.state = PROCESS_READY; assert(process_core_pick(&s, 1) == b);
    s.tasks[b].info.state = PROCESS_READY; assert(process_core_pick(&s, 1) == 0);
    s.tasks[a].info.state = PROCESS_SLEEPING; s.tasks[a].sleep_start = UINT64_MAX - 1; s.tasks[a].sleep_ticks = 4;
    process_core_tick(&s, 0); assert(s.tasks[a].info.state == PROCESS_SLEEPING);
    process_core_tick(&s, 2); assert(s.tasks[a].info.state == PROCESS_READY);
    s.tasks[0].info.state = PROCESS_WAITING; s.tasks[0].waiting_for = 2;
    assert(process_core_finish(&s, 2, 42) && s.tasks[0].info.state == PROCESS_READY);
    assert(!process_core_finish(&s, 2, 0));
    s.tasks[0].info.state = PROCESS_RUNNING;
    s.tasks[a].stack = 4096;
    assert(!process_core_collect(&s, 2, 0)); /* Nao libera registro antes da pilha. */
    s.tasks[a].stack = 0;
    int status;
    assert(process_core_collect(&s, 2, &status) && status == 42);
    assert(!process_core_collect(&s, 2, &status) && !process_core_finish(&s, 999, 0));
    int replacement = process_core_create(&s, "replacement");
    assert(replacement == a && s.tasks[a].info.pid == 4 && process_core_find(&s, 2) < 0);
    s.tasks[0].info.state = PROCESS_READY; assert(process_core_pick(&s, 3) == a);
    int child = process_core_create(&s, "grandchild");
    assert(child > 0 && s.tasks[child].info.parent == 4);
    assert(!process_core_collect(&s, 3, 0)); /* Nao e filho do processo atual. */
    assert(process_core_finish(&s, 4, -9) && s.tasks[child].info.parent == 1);
    assert(process_core_pick(&s, 3) == b);
    s.tasks[b].info.state = PROCESS_READY;
    while (s.current != 0) {
        int next = process_core_pick(&s, 3); assert(next >= 0);
        if (next != 0) s.tasks[next].info.state = PROCESS_READY;
    }
    assert(process_core_collect(&s, 4, &status) && status == -9);
    for (unsigned int i = 1; i < PROCESS_MAX; ++i) if (s.tasks[i].info.state != PROCESS_UNUSED) {
        assert(process_core_finish(&s, s.tasks[i].info.pid, 0));
        assert(process_core_collect(&s, s.tasks[i].info.pid, 0));
    }
    uint64_t previous = s.next_pid;
    for (unsigned int run = 0; run < 2000; ++run) {
        int slot = process_core_create(&s, "cycle"); assert(slot > 0);
        uint64_t pid = s.tasks[slot].info.pid; assert(pid == previous++);
        assert(process_core_finish(&s, pid, 0) && process_core_collect(&s, pid, 0));
    }
    for (unsigned int i = 1; i < PROCESS_MAX; ++i) assert(process_core_create(&s, "full") == (int)i);
    assert(process_core_free_slot(&s) < 0 && process_core_create(&s, "overflow") < 0);
    for (unsigned int i = 0; i < PROCESS_MAX; ++i) s.tasks[i].info.state = PROCESS_WAITING;
    assert(process_core_pick(&s, 100) < 0);
    s.tasks[1] = (struct process_record){0}; s.next_pid = UINT64_MAX;
    assert(process_core_create(&s, "pid-wrap") < 0);
    puts("OK: processos, round-robin, estados, sono/wrap, wait, orfaos, slots e PIDs.");
}
