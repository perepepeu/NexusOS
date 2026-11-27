#include <process_internal.h>
#include <interrupts.h>
#include <io.h>
#include <vmm.h>
#include <pmm.h>
#include <panic.h>
#include <log.h>
#include <process_resources.h>
#include <heap.h>

enum { STACK_PAGES = 4, STACK_STRIDE = 6 * 4096 };
static struct process_manager manager;
static uint64_t stack_window;
static int fail_after = -1;
extern void process_context_switch(uintptr_t *old, uintptr_t next);
extern unsigned int process_register_test(void);

static void release_stack(struct thread_control_block *p) {
    if (!p->stack) return;
    for (unsigned int j = 0; j < STACK_PAGES; ++j) {
        uint64_t physical;
        KASSERT(vmm_unmap_page(p->stack + j * 4096, &physical) == VMM_OK);
        KASSERT(pmm_free_page(physical) == PMM_OK);
    }
    p->stack = p->context = 0;
}
static void reap_stacks(void) {
    for (unsigned int i = 1; i < THREAD_MAX; ++i)
        if (i != manager.current && manager.tasks[i].info.state == PROCESS_TERMINATED) release_stack(&manager.tasks[i]);
    for (unsigned int i = 0; i < PROCESS_MAX; ++i)
        if (manager.processes[i].used && manager.processes[i].info.state == PROCESS_TERMINATED &&
                manager.processes[i].info.pid != process_current_pid()) process_resources_reap(manager.processes[i].info.pid);
}
bool process_owner_alive(uint64_t pid) {
    int pcb = process_core_find(&manager, pid);
    return pid && pcb >= 0 && manager.processes[pcb].info.state != PROCESS_TERMINATED;
}
static void schedule(void) {
    unsigned int old = manager.current;
    reap_stacks();
    int next;
    while ((next = process_core_pick(&manager, timer_ticks())) < 0)
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    if ((unsigned int)next != old) {
        process_context_switch(&manager.tasks[old].context, manager.tasks[next].context);
        reap_stacks();
    }
}
__attribute__((noreturn)) static void trampoline(void) {
    reap_stacks();
    struct thread_control_block *p = &manager.tasks[manager.current];
    irq_restore(1u << 9);
    p->entry(p->argument);
    thread_exit(0);
}
bool process_init(void) {
    struct vmm_stats v;
    vmm_get_stats(&v);
    uint64_t flags = irq_save();
    if (manager.ready || !v.ready || v.window_size < (1ull << 31) + THREAD_MAX * STACK_STRIDE) { irq_restore(flags); return false; }
    stack_window = v.window_base + (1ull << 31);
    bool result = process_core_init(&manager); irq_restore(flags); return result;
}
static uint64_t create(const char *name, void (*entry)(void *), void *argument, bool thread) {
    if (!manager.ready || !name || !*name || !entry) return 0;
    uint64_t flags = irq_save();
    KASSERT(flags & (1u << 9));
    int slot = process_core_free_slot(&manager);
    if (slot < 0) { irq_restore(flags); return 0; }
    uintptr_t stack = stack_window + (unsigned int)slot * STACK_STRIDE + 4096;
    unsigned int mapped = 0;
    for (; mapped < STACK_PAGES; ++mapped) {
        uint64_t physical;
        if (fail_after == 0) break;
        if (fail_after > 0) --fail_after;
        if (vmm_alloc_page(stack + mapped * 4096, VMM_WRITE, &physical) != VMM_OK) break;
    }
    if (mapped != STACK_PAGES) {
        while (mapped) {
            uint64_t physical;
            KASSERT(vmm_unmap_page(stack + --mapped * 4096, &physical) == VMM_OK);
            KASSERT(pmm_free_page(physical) == PMM_OK);
        }
        irq_restore(flags); return 0;
    }
    int created = thread ? thread_core_create(&manager, name) : process_core_create(&manager, name);
    if (created < 0) {
        struct thread_control_block temporary = {.stack = stack}; release_stack(&temporary);
        irq_restore(flags); return 0;
    }
    KASSERT(created == slot);
    struct thread_control_block *p = &manager.tasks[slot];
    p->stack = stack; p->entry = entry; p->argument = argument;
    /* Seis callee-saved + RET; RSP%16=8 ao entrar no trampoline SysV. */
    uintptr_t *context = (uintptr_t *)(stack + STACK_PAGES * 4096 - 64);
    for (unsigned int i = 0; i < 8; ++i) context[i] = 0;
    context[6] = (uintptr_t)trampoline; p->context = (uintptr_t)context;
    uint64_t id = thread ? p->info.tid : p->info.pid;
    irq_restore(flags); return id;
}
uint64_t process_create(const char *name, void (*entry)(void *), void *argument) { return create(name, entry, argument, false); }
uint64_t thread_create(const char *name, void (*entry)(void *), void *argument) { return create(name, entry, argument, true); }
uint64_t process_current_pid(void) { return manager.ready ? manager.tasks[manager.current].info.pid : 0; }
uint64_t thread_current_tid(void) { return manager.ready ? manager.tasks[manager.current].info.tid : 0; }
void process_on_tick(uint64_t now) { process_core_tick(&manager, now); }
void process_yield(void) {
    if (!manager.ready) return;
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    KASSERT(manager.tasks[manager.current].info.state == PROCESS_RUNNING);
    manager.tasks[manager.current].info.state = PROCESS_READY;
    schedule(); irq_restore(flags);
}
void process_sleep(uint64_t ticks) {
    if (!ticks) { process_yield(); return; }
    KASSERT(manager.ready && ticks <= INT64_MAX);
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    KASSERT(process_core_block(&manager, PROCESS_BLOCK_SLEEP, ticks, timer_ticks()));
    schedule(); irq_restore(flags);
}
bool process_kill(uint64_t pid, int status) {
    if (!manager.ready) return false;
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    bool result = pid != process_current_pid() && process_core_finish(&manager, pid, status);
    reap_stacks(); irq_restore(flags); return result;
}
__attribute__((noreturn)) void process_exit(int status) {
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    KASSERT(manager.ready && process_core_finish(&manager, process_current_pid(), status));
    schedule(); PANIC(ERR_PROCESS, "Processo encerrado voltou a executar.");
}
__attribute__((noreturn)) void thread_exit(int status) {
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    KASSERT(manager.ready && thread_core_finish(&manager, thread_current_tid(), status));
    schedule(); PANIC(ERR_PROCESS, "Thread encerrada voltou a executar.");
}
bool process_wait(uint64_t child, int *status) {
    if (!manager.ready) return false;
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    for (;;) {
        int slot = process_core_find(&manager, child);
        if (slot < 0 || manager.processes[slot].system || child == process_current_pid() ||
                manager.processes[slot].info.parent != process_current_pid()) { irq_restore(flags); return false; }
        if (manager.processes[slot].info.state == PROCESS_TERMINATED) break;
        KASSERT(process_core_block(&manager, PROCESS_BLOCK_WAIT, child, timer_ticks()));
        schedule();
    }
    reap_stacks(); KASSERT(process_resource_count(child) == 0);
    bool result = process_core_collect(&manager, child, status);
    irq_restore(flags); return result;
}
bool thread_join(uint64_t tid, int *status) {
    if (!manager.ready) return false;
    uint64_t flags = irq_save(); KASSERT(flags & (1u << 9));
    for (;;) {
        int slot = thread_core_find(&manager, tid);
        if (slot < 0 || (unsigned int)slot == manager.current || manager.tasks[slot].system ||
                manager.tasks[slot].info.pid != process_current_pid()) { irq_restore(flags); return false; }
        if (manager.tasks[slot].info.state == PROCESS_TERMINATED) break;
        if (!process_core_block(&manager, PROCESS_BLOCK_JOIN, tid, timer_ticks())) { irq_restore(flags); return false; }
        schedule();
    }
    reap_stacks(); bool result = thread_core_collect(&manager, tid, status);
    irq_restore(flags); return result;
}
size_t process_snapshot(struct process_info *out, size_t capacity) {
    if (!out || !manager.ready) return 0;
    uint64_t flags = irq_save(); size_t count = 0;
    for (unsigned int i = 0; i < PROCESS_MAX && count < capacity; ++i)
        if (manager.processes[i].used) {
            out[count] = manager.processes[i].info;
            out[count++].resources = process_resource_count(manager.processes[i].info.pid);
        }
    irq_restore(flags); return count;
}
size_t thread_snapshot(struct thread_info *out, size_t capacity) {
    if (!out || !manager.ready) return 0;
    uint64_t flags = irq_save(); size_t count = 0;
    for (unsigned int i = 0; i < THREAD_MAX && count < capacity; ++i)
        if (manager.tasks[i].info.state != PROCESS_UNUSED) out[count++] = manager.tasks[i].info;
    irq_restore(flags); return count;
}

static volatile unsigned int order[12], order_count;
static void test_worker(void *argument) {
    uintptr_t value = (uintptr_t)argument;
    volatile uint64_t local = 0x123456789abcdef0ull ^ value;
    for (unsigned int i = 0; i < 3; ++i) {
        KASSERT(order_count < 12); order[order_count++] = (unsigned int)value;
        process_yield();
        KASSERT(local == (0x123456789abcdef0ull ^ value));
    }
    KASSERT(process_register_test());
    process_exit((int)value);
}
static void sleep_worker(void *argument) { (void)argument; for (;;) process_sleep(100); }
static volatile unsigned int thread_mask;
static uint64_t group_pid;
static unsigned char *group_buffer;
static void group_worker(void *argument) {
    KASSERT(process_current_pid() == group_pid && thread_current_tid() != 0);
    if ((uintptr_t)argument == 4) KASSERT(group_buffer && group_buffer[0] == 0xa5);
    process_sleep(2);
    thread_mask |= (unsigned int)(uintptr_t)argument;
    if ((uintptr_t)argument == 1) thread_exit(11);
}
static void group_entry(void *argument) {
    (void)argument; group_pid = process_current_pid();
    group_buffer = kmalloc(64); KASSERT(group_buffer); group_buffer[0] = 0xa5;
    uint64_t a = thread_create("thread-a", group_worker, (void *)1);
    uint64_t b = thread_create("thread-b", group_worker, (void *)2);
    KASSERT(a && b && a != b && a != thread_current_tid());
    int status;
    KASSERT(!thread_join(thread_current_tid(), 0));
    KASSERT(thread_join(a, &status) && status == 11);
    KASSERT(thread_join(b, &status) && status == 0 && !thread_join(b, 0));
    KASSERT(thread_create("survivor", group_worker, (void *)4));
    /* Retorno termina apenas esta thread; survivor mantem o PCB vivo. */
}
static unsigned int custom_releases;
static unsigned char *transferred;
static volatile bool resources_ready;
static void resource_cleanup(void *pointer) { KASSERT(pointer == &custom_releases); ++custom_releases; }
static void resource_worker(void *argument) {
    unsigned char *buffer = kmalloc(31), *zero = kcalloc(257, 3);
    KASSERT(buffer && zero);
    for (size_t i = 0; i < 771; ++i) KASSERT(zero[i] == 0);
    for (size_t i = 0; i < 31; ++i) buffer[i] = (unsigned char)i;
    unsigned char *grown = krealloc(buffer, 8193); KASSERT(grown); buffer = grown;
    KASSERT(!krealloc(buffer, SIZE_MAX));
    for (size_t i = 0; i < 31; ++i) KASSERT(buffer[i] == i);
    kfree(zero);
    uint64_t pages = process_alloc_pages(3, 4); KASSERT(pages && pmm_page_allocated(pages));
    KASSERT(process_resource_release((void *)(uintptr_t)pages));
    KASSERT(!process_resource_release((void *)(uintptr_t)pages));
    KASSERT(process_alloc_pages(5, 1));
    unsigned char *mapping = process_map_pages(3, VMM_WRITE); KASSERT(mapping);
    KASSERT(mapping[0] == 0 && mapping[12287] == 0); mapping[12287] = 0x5a;
    KASSERT(!process_map_pages(1, VMM_WRITE | VMM_EXEC));
    KASSERT(!process_map_pages(SIZE_MAX, VMM_WRITE));
    KASSERT(process_resource_attach(&custom_releases, resource_cleanup));
    KASSERT(!process_resource_attach(&custom_releases, resource_cleanup));
    KASSERT(process_resource_release(&custom_releases));
    KASSERT(process_resource_attach(&custom_releases, resource_cleanup));
    KASSERT(!process_resource_transfer(buffer, UINT64_MAX));
    KASSERT(process_resource_transfer(buffer, 1)); transferred = buffer;
    if (argument) KASSERT(thread_create("owned-sibling", sleep_worker, 0));
    resources_ready = true;
    if (argument) for (;;) process_sleep(100);
    /* Nenhum free explicito: recursos restantes pertencem ao PCB. */
}
static void resource_limit_worker(void *argument) {
    (void)argument;
    for (unsigned int i = 0; i < 256; ++i) KASSERT(kmalloc(16));
    KASSERT(process_resource_count(process_current_pid()) == 256);
    KASSERT(!kmalloc(16) && !process_alloc_pages(1, 1) && !process_map_pages(1, VMM_WRITE));
}
static void guard_worker(void *argument) {
    uint64_t address = (uintptr_t)argument;
    log_info("test", "Pilha de processo: guarda em %p PID=%llu", (void *)(uintptr_t)address,
        (unsigned long long)process_current_pid());
    __asm__ volatile ("movb $1, (%0)" : : "r"(address) : "memory");
    PANIC(ERR_SELFTEST, "Guarda da pilha deveria gerar page fault.");
}
void process_selftest(bool guard_fault) {
    if (guard_fault) {
        uint64_t pid = process_create("guard-fault", guard_worker, (void *)(uintptr_t)(stack_window + STACK_STRIDE));
        KASSERT(pid && process_wait(pid, 0));
        PANIC(ERR_SELFTEST, "Processo de teste nao gerou page fault.");
    }
    struct pmm_stats pb, pa; struct vmm_stats vb, va;
    pmm_get_stats(&pb); vmm_get_stats(&vb);
    fail_after = 2; KASSERT(!process_create("oom", sleep_worker, 0)); fail_after = -1;
    pmm_get_stats(&pa); vmm_get_stats(&va);
    KASSERT(pb.free_pages == pa.free_pages && vb.mapped_pages == va.mapped_pages && vb.table_pages == va.table_pages);
    order_count = 0;
    uint64_t a = process_create("worker-a", test_worker, (void *)1);
    uint64_t b = process_create("worker-b", test_worker, (void *)2);
    KASSERT(a && b && a != b);
    struct vmm_mapping mapping;
    int slot = -1;
    for (unsigned int i = 0; i < THREAD_MAX; ++i) if (manager.tasks[i].info.pid == a) { slot = (int)i; break; }
    KASSERT(slot > 0);
    KASSERT(!vmm_translate(manager.tasks[slot].stack - 4096, &mapping));
    KASSERT(!vmm_translate(manager.tasks[slot].stack + STACK_PAGES * 4096, &mapping));
    KASSERT(vmm_translate(manager.tasks[slot].stack, &mapping) && mapping.flags == VMM_WRITE);
    int status;
    KASSERT(process_wait(a, &status) && status == 1);
    KASSERT(process_wait(b, &status) && status == 2);
    KASSERT(order_count == 6);
    for (unsigned int i = 0; i < 6; ++i) KASSERT(order[i] == i % 2 + 1);
    KASSERT(!process_wait(a, &status) && !process_kill(1, -9));
    a = process_create("sleep-kill", sleep_worker, 0); KASSERT(a);
    process_yield();
    KASSERT(manager.processes[process_core_find(&manager, a)].info.state == PROCESS_BLOCKED);
    KASSERT(process_kill(a, -9) && !process_kill(a, -9));
    KASSERT(process_wait(a, &status) && status == -9);
    thread_mask = 0;
    a = process_create("thread-group", group_entry, 0); KASSERT(a);
    KASSERT(process_wait(a, &status) && status == 0 && thread_mask == 7);
    log_info("test", "Threads: TIDs distintos, PID compartilhado, join e ultima thread OK.");
    custom_releases = 0;
    for (unsigned int mode = 0; mode < 2; ++mode) {
        resources_ready = false; transferred = 0;
        a = process_create("owned-resources", resource_worker, (void *)(uintptr_t)mode); KASSERT(a);
        if (mode) {
            process_yield(); KASSERT(resources_ready && process_kill(a, -9));
        }
        KASSERT(process_wait(a, &status) && status == (mode ? -9 : 0));
        KASSERT(process_resource_count(a) == 0 && transferred);
        for (unsigned int i = 0; i < 31; ++i) KASSERT(transferred[i] == i);
        kfree(transferred);
    }
    KASSERT(custom_releases == 4);
    a = process_create("resource-limit", resource_limit_worker, 0); KASSERT(a && process_wait(a, 0));
    KASSERT(!process_resource_count(a));
    log_info("test", "Recursos: heap automatico, paginas, mapas, destrutores, transferencia, kill e OOM OK.");
    uint64_t start = timer_ticks(); process_sleep(2); KASSERT(timer_ticks() - start >= 2);
    pmm_get_stats(&pa); vmm_get_stats(&va);
    KASSERT(pb.free_pages == pa.free_pages && pb.allocated_pages == pa.allocated_pages &&
        vb.mapped_pages == va.mapped_pages && vb.table_pages == va.table_pages);
    struct process_info list[PROCESS_MAX];
    KASSERT(process_snapshot(list, PROCESS_MAX) == 1 && list[0].pid == 1);
    log_info("test", "Processos: contexto, round-robin, sono, wait, kill, guardas e limpeza OK.");
}
