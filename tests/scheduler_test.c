#include <process_internal.h>
#include <assert.h>
#include <stdio.h>
static struct process_manager s;
static int yield(void) {
    s.tasks[s.current].info.state = PROCESS_READY;
    int next = process_core_pick(&s, 0); assert(next >= 0 && process_core_queue_valid(&s)); return next;
}
int main(void) {
    assert(process_core_init(&s) && process_core_add_idle(&s));
    int a = process_core_create(&s, "a"), b = process_core_create(&s, "b"), c = process_core_create(&s, "c");
    assert(s.ready_count == 3 && process_core_queue_valid(&s));
    for (unsigned int n = 1; n < SCHEDULER_QUANTUM_TICKS; ++n) {
        process_core_tick(&s, n); assert(!process_core_should_preempt(&s));
    }
    process_core_tick(&s, SCHEDULER_QUANTUM_TICKS); assert(process_core_should_preempt(&s));
    assert(yield() == a && !s.slice_ticks);
    assert(yield() == b && yield() == c && yield() == 0);
    assert(process_core_block(&s, PROCESS_BLOCK_SLEEP, 2, 10));
    assert(process_core_pick(&s, 10) == a && process_core_queue_valid(&s));
    process_core_tick(&s, 12); assert(s.queued[0]);
    assert(yield() == b && yield() == c && yield() == 0); /* Wake entrou no fim da FIFO. */
    uint64_t tid = s.tasks[b].info.tid;
    assert(thread_core_suspend(&s, tid) && !s.queued[b] && !thread_core_suspend(&s, tid));
    assert(thread_core_resume(&s, tid) && s.queued[b] && !thread_core_resume(&s, tid));
    uint64_t dead = s.tasks[c].info.pid;
    assert(process_core_finish(&s, dead, -9) && !s.queued[c]);
    assert(process_core_collect(&s, dead, 0));
    int reused = process_core_create(&s, "reused"); assert(reused == c && s.queued[c]);
    assert(process_core_queue_valid(&s));
    s.tasks[0].preempt_depth = 2;
    for (unsigned int n = 0; n < SCHEDULER_QUANTUM_TICKS; ++n) process_core_tick(&s, 20 + n);
    assert(!process_core_should_preempt(&s) && s.tasks[0].reschedule_pending);
    assert(!process_core_block(&s, PROCESS_BLOCK_SLEEP, 1, 25));
    --s.tasks[0].preempt_depth; assert(!process_core_should_preempt(&s));
    --s.tasks[0].preempt_depth; assert(process_core_should_preempt(&s) && s.deferred_ticks == 2);
    s = (struct process_manager){0}; assert(process_core_init(&s) && process_core_add_idle(&s));
    for (unsigned int i = 2; i < THREAD_MAX; ++i) assert(thread_core_create(&s, "full") == (int)i);
    assert(s.ready_count == THREAD_MAX - 2 && thread_core_create(&s, "overflow") < 0);
    for (unsigned int i = 0; i < 5000; ++i) { yield(); assert(s.ready_count == THREAD_MAX - 2); }
    for (uint64_t id = 2; id < THREAD_MAX; ++id) {
        assert(thread_core_finish(&s, id, 0));
        if (s.tasks[s.current].info.state == PROCESS_TERMINATED) assert(process_core_pick(&s, 0) >= 0);
        assert(thread_core_collect(&s, id, 0)); assert(process_core_queue_valid(&s));
    }
    assert(!s.ready_count && s.current == 0);
    assert(process_core_block(&s, PROCESS_BLOCK_INPUT, 0, 0));
    assert(process_core_pick(&s, 0) == 1 && !s.ready_count);
    process_core_input_wake(&s); assert(process_core_should_preempt(&s));
    assert(yield() == 0 && !s.ready_count);
    puts("OK: FIFO, quantum, round-robin, wake, remocao, suspensao, deferral e 5000 ciclos.");
}
