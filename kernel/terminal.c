#include <stdbool.h>
#include <stdint.h>
#include <console.h>
#include <interrupts.h>
#include <keyboard.h>
#include <log.h>
#include <serial.h>
#include <terminal.h>
#include <version.h>
#include <power.h>
#include <memory.h>
#include <pmm.h>
#include <vmm.h>
#include <heap.h>
#include <process.h>
#include <io.h>
#include <user_mode.h>

enum { LINE_SIZE = 128, HISTORY_SIZE = 8 };
static const char prompt[] = "nexus> ";
static char history[HISTORY_SIZE][LINE_SIZE];
static unsigned int history_head, history_count;

static void put(const char *text) { uint64_t flags = irq_save(); console_write(text); serial_write(text); irq_restore(flags); }

static void number(uint64_t value, bool both) {
    char digits[21], text[2] = {0, 0};
    unsigned int count = 0;
    do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
    while (count) { text[0] = digits[--count]; if (both) put(text); else serial_write(text); }
}

static void hex(uint64_t value) {
    const char digits[] = "0123456789abcdef";
    char buffer[16], text[2] = {0, 0};
    unsigned int used = 0;
    do { buffer[used++] = digits[value & 15]; value >>= 4; } while (value);
    put("0x");
    while (used) { text[0] = buffer[--used]; put(text); }
}

static void memory_bytes(const char *label, uint64_t bytes) {
    put(label); number(bytes, true); put(" bytes ("); number(bytes / (1024 * 1024), true); put(" MiB)\n");
}

static void memory_summary(void) {
    const struct memory_stats *stats = memory_get_stats();
    if (!stats->valid) { put("Mapa de memoria indisponivel.\n"); return; }
    put("Memoria fisica - mapa Limine\nRegioes: "); number(stats->region_count, true); put("\n");
    memory_bytes("RAM total identificada: ", stats->ram_bytes);
    memory_bytes("Utilizavel no boot: ", stats->bytes[MEMORY_USABLE]);
    put("Paginas utilizaveis de 4 KiB: "); number(stats->usable_pages, true); put("\n");
    memory_bytes("Bootloader (reclamavel depois): ", stats->bytes[MEMORY_BOOTLOADER]);
    memory_bytes("ACPI (reclamavel depois): ", stats->bytes[MEMORY_ACPI]);
    memory_bytes("Kernel e modulos: ", stats->bytes[MEMORY_KERNEL]);
    memory_bytes("ACPI NVS: ", stats->bytes[MEMORY_NVS]);
    memory_bytes("Reservada/MMIO: ", stats->bytes[MEMORY_RESERVED]);
    memory_bytes("Framebuffer: ", stats->bytes[MEMORY_FRAMEBUFFER]);
    memory_bytes("Memoria defeituosa: ", stats->bytes[MEMORY_BAD]);
    memory_bytes("Tipo desconhecido (reservado): ", stats->bytes[MEMORY_UNKNOWN]);
    put("Maior bloco paginado: "); hex(stats->largest_base); put(" tamanho=");
    number(stats->largest_bytes, true); put(" bytes\n");
    struct pmm_stats allocator;
    pmm_get_stats(&allocator);
    memory_bytes("Livre no alocador: ", allocator.free_pages * MEMORY_PAGE_SIZE);
    put("Bootloader e ACPI ainda nao foram reclamados.\n");
}

static void page_summary(void) {
    struct pmm_stats s;
    pmm_get_stats(&s);
    if (!s.ready) { put("Alocador de paginas indisponivel.\n"); return; }
    put("Alocador fisico de paginas de 4 KiB\nPaginas totais: "); number(s.total_pages, true);
    put("\nPaginas livres: "); number(s.free_pages, true);
    put("\nPaginas alocadas: "); number(s.allocated_pages, true);
    put("\nPaginas de metadados: "); number(s.metadata_pages, true);
    put("\nBitmap fisico: "); hex(s.metadata_address);
    put("\nBytes do bitmap: "); number(s.bitmap_bytes, true);
    put("\nFaixas utilizaveis: "); number(s.range_count, true); put("\n");
}

static void virtual_summary(void) {
    struct vmm_stats s;
    vmm_get_stats(&s);
    if (!s.ready) { put("Memoria virtual indisponivel.\n"); return; }
    put("Memoria virtual do kernel - 4 niveis\nCR3 fisico: "); hex(s.root_address);
    put("\nJanela propria: "); hex(s.window_base);
    put("\nTamanho da janela: "); number(s.window_size, true); put(" bytes\nPaginas mapeadas: ");
    number(s.mapped_pages, true); put("\nTabelas proprias: "); number(s.table_pages, true);
    put(" paginas\nPaginas: 4 KiB, supervisor, NX e WP ativos.\n");
}

static void heap_summary(void) {
    heap_check();
    struct heap_stats s; heap_get_stats(&s);
    put("Integridade do heap: OK.\n");
    put("Heap do kernel - alinhamento 16 bytes\nAlocacoes ativas: "); number(s.allocations, true);
    put("\nArenas ativas: "); number(s.arenas, true);
    memory_bytes("\nBytes solicitados: ", s.requested_bytes);
    memory_bytes("Bytes mapeados: ", s.mapped_bytes);
    put("Limite virtual: 64 MiB; paginas RW/NX sob demanda.\n");
}

static void memory_map(void) {
    const struct memory_stats *stats = memory_get_stats();
    if (!stats->valid) { put("Mapa de memoria indisponivel.\n"); return; }
    for (unsigned int i = 0; i < stats->region_count; ++i) {
        const struct limine_memmap_entry *entry = memory_get_region(i);
        put("Regiao "); number(i, true); put(": base="); hex(entry->base);
        put(" tamanho="); number(entry->length, true); put(" tipo="); number(entry->type, true);
        put(" "); put(memory_type_name(entry->type)); put("\n");
    }
}

static void copy(char *destination, const char *source) {
    unsigned int i = 0;
    while (i + 1 < LINE_SIZE && source[i]) { destination[i] = source[i]; ++i; }
    destination[i] = 0;
}

static unsigned int length(const char *text) {
    unsigned int count = 0;
    while (text[count]) ++count;
    return count;
}

static bool equal(const char *left, const char *right) {
    while (*left && *right) {
        char c = *left++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != *right++) return false;
    }
    return !*left && !*right;
}

static void redraw(const char *line, unsigned int cursor) {
    uint64_t flags = irq_save();
    console_input(prompt, line, cursor);
    serial_write("\r\x1b[2K"); serial_write(prompt); serial_write(line);
    serial_write("\r\x1b["); number(sizeof prompt - 1 + cursor, false); serial_write("C");
    irq_restore(flags);
}

static void clear_screen(void) {
    uint64_t flags = irq_save();
    console_clear();
    serial_write("\x1b[2J\x1b[H");
    irq_restore(flags);
}

static unsigned int history_slot(unsigned int position) {
    return (history_head + HISTORY_SIZE - history_count + position) % HISTORY_SIZE;
}

static void signed_number(int value) {
    if (value < 0) { put("-"); number((uint64_t)(-(int64_t)value), true); }
    else number((uint64_t)value, true);
}
static bool pid_argument(const char *text, uint64_t *pid) {
    uint64_t value = 0;
    if (!*text) return false;
    while (*text) {
        if (*text < '0' || *text > '9' || value > (UINT64_MAX - (unsigned int)(*text - '0')) / 10) return false;
        value = value * 10 + (unsigned int)(*text++ - '0');
    }
    *pid = value; return value != 0;
}
static void process_summary(void) {
    struct process_info list[PROCESS_MAX];
    size_t count = process_snapshot(list, PROCESS_MAX);
    put("Processos preemptivos\nPID PPID ESTADO THREADS TICKS SAIDA NOME MODO\n");
    for (size_t i = 0; i < count; ++i) {
        number(list[i].pid, true); put(" "); number(list[i].parent, true); put(" ");
        put(process_state_name(list[i].state)); put(" "); number(list[i].threads, true); put(" "); number(list[i].cpu_ticks, true); put(" ");
        signed_number(list[i].exit_status); put(" "); put(list[i].name); put(list[i].user_mode ? " R3\n" : " R0\n");
    }
}
static void thread_summary(void) {
    struct thread_info list[THREAD_MAX];
    size_t count = thread_snapshot(list, THREAD_MAX);
    put("Threads do kernel\nTID PID ESTADO TICKS SAIDA NOME\n");
    for (size_t i = 0; i < count; ++i) {
        number(list[i].tid, true); put(" "); number(list[i].pid, true); put(" ");
        put(process_state_name(list[i].state)); put(" "); number(list[i].cpu_ticks, true); put(" ");
        signed_number(list[i].exit_status); put(" "); put(list[i].name); put("\n");
    }
}
static void demo_process(void *argument) {
    (void)argument;
    for (unsigned int i = 0; i < 5; ++i) process_sleep(100);
}
static void scheduler_summary(void) {
    struct scheduler_info s; scheduler_snapshot(&s);
    put("Escalonador: Round Robin preemptivo\nPIT: 100 Hz\nQuantum: "); number(s.quantum_ticks, true);
    put(" ticks ("); number(s.quantum_ticks * 1000 / SCHEDULER_PIT_HZ, true);
    put(" ms)\nFila FIFO de prontas: "); number(s.ready, true);
    put("\nTrocas de contexto: "); number(s.switches, true);
    put("\nPreempcoes: "); number(s.preemptions, true);
    put("\nTicks adiados em regioes criticas: "); number(s.deferred_ticks, true); put("\n");
}

static void save_history(const char *line) {
    copy(history[history_head], line);
    history_head = (history_head + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) ++history_count;
}

static void command(char *line, const char *bootloader, const char *boot_version) {
    char *name = line;
    while (*name == ' ' || *name == '\t') ++name;
    if (!*name) return;
    save_history(line);
    char *args = name;
    while (*args && *args != ' ' && *args != '\t') ++args;
    if (*args) *args++ = 0;
    while (*args == ' ' || *args == '\t') ++args;
    if (equal(name, "echo")) { put(args); put("\n"); return; }
    if (equal(name, "kill") || equal(name, "wait")) {
        uint64_t pid;
        if (!pid_argument(args, &pid)) { put("Uso: "); put(name); put(" PID (inteiro positivo).\n"); return; }
        if (equal(name, "kill")) {
            if (process_kill(pid, -9)) { put("Processo encerrado: "); number(pid, true); put("\n"); }
            else put("Processo inexistente, encerrado ou protegido.\n");
        } else {
            int status;
            if (process_wait(pid, &status)) {
                put("Processo coletado: "); number(pid, true); put(" status="); signed_number(status); put("\n");
            } else put("Filho inexistente ou ja coletado.\n");
        }
        return;
    }
    bool known = equal(name, "help") || equal(name, "clear") || equal(name, "version")
        || equal(name, "uptime") || equal(name, "sysinfo") || equal(name, "history")
        || equal(name, "reboot") || equal(name, "shutdown")
        || equal(name, "memory") || equal(name, "meminfo") || equal(name, "memtest")
        || equal(name, "memmap") || equal(name, "pmm") || equal(name, "vmm") || equal(name, "heap")
        || equal(name, "ps") || equal(name, "threads") || equal(name, "spawn") || equal(name, "spawnuser") || equal(name, "sched");
    if (!known) { put("Comando desconhecido: "); put(name); put(". Digite help.\n"); return; }
    if (*args) { put("Uso: "); put(name); put(" (sem argumentos).\n"); return; }
    if (equal(name, "help")) {
        put("Comandos: help clear echo version uptime sysinfo history\n");
        put("Energia: reboot shutdown\n");
        put("Memoria: memory meminfo memmap pmm vmm heap memtest\n");
        put("Processos: ps threads spawn spawnuser kill PID wait PID sched\n");
        put("Edicao: setas, Home/End, Delete, Backspace, historico Up/Down.\n");
        put("Ctrl+C cancela, Ctrl+U apaga a linha, Ctrl+L limpa a tela.\n");
    } else if (equal(name, "ps")) process_summary();
    else if (equal(name, "sched")) scheduler_summary();
    else if (equal(name, "threads")) thread_summary();
    else if (equal(name, "spawnuser")) {
        uint64_t pid = process_user_demo();
        if (pid) { put("Processo Ring3 criado: "); number(pid, true); put(" (saida esperada: 42)\n"); }
        else put("Falha ao criar processo de usuario.\n");
    }
    else if (equal(name, "spawn")) {
        uint64_t pid = process_create("demo", demo_process, 0);
        if (pid) { put("Processo criado: "); number(pid, true); put(" (demo, duracao 5s)\n"); }
        else put("Falha ao criar processo: memoria ou slots insuficientes.\n");
    }
    else if (equal(name, "memory") || equal(name, "meminfo")) memory_summary();
    else if (equal(name, "memtest")) {
        put("Memtest: teste limitado a 64 paginas proprias e blocos temporarios do heap.\n");
        if (memory_test_run()) put("Memtest: PMM, VMM e heap OK; sem vazamentos.\n");
        else put("Memtest: FALHOU (memoria insuficiente, dados ou contadores invalidos).\n");
    }
    else if (equal(name, "memmap")) memory_map();
    else if (equal(name, "pmm")) page_summary();
    else if (equal(name, "vmm")) virtual_summary();
    else if (equal(name, "heap")) heap_summary();
    else if (equal(name, "reboot")) {
        if (!power_reboot()) put("Nao foi possivel reiniciar; terminal continua ativo.\n");
    } else if (equal(name, "shutdown")) {
        if (!power_shutdown()) put("Nao foi possivel desligar; terminal continua ativo.\n");
    } else if (equal(name, "clear")) clear_screen();
    else if (equal(name, "version")) put("NexusOS " NEXUS_VERSION " (x86_64)\n");
    else if (equal(name, "uptime")) {
        uint64_t ticks = timer_ticks();
        put("Uptime: "); number(ticks / 100, true); put(".");
        if (ticks % 100 < 10) put("0");
        number(ticks % 100, true); put("s\n");
    } else if (equal(name, "sysinfo")) {
        put("Sistema: NexusOS " NEXUS_VERSION "\nArquitetura: x86_64\nBootloader: ");
        put(bootloader); put(" "); put(boot_version);
        put("\nTimer: PIT 100 Hz\nTeclado: PS/2 US, IRQ1\nDescartes do teclado: ");
        number(keyboard_dropped(), true); put("\n");
        put("Memoria utilizavel: "); number(memory_get_stats()->bytes[MEMORY_USABLE] / (1024 * 1024), true);
        put(" MiB\n");
        struct pmm_stats pages;
        pmm_get_stats(&pages);
        put("Paginas fisicas livres: "); number(pages.free_pages, true); put("\n");
    } else if (equal(name, "history")) {
        for (unsigned int i = 0; i < history_count; ++i) {
            number(i + 1, true); put(" "); put(history[history_slot(i)]); put("\n");
        }
    }
}

__attribute__((noreturn)) void terminal_run(const char *bootloader, const char *boot_version) {
    char line[LINE_SIZE] = {0}, draft[LINE_SIZE] = {0};
    unsigned int used = 0, cursor = 0, navigation = 0, dropped = keyboard_dropped();
    bool limit_warned = false;
    log_info("terminal", "Terminal pronto. Digite help.");
    redraw(line, cursor);
    for (;;) {
        struct key_event event;
        keyboard_wait(&event);
        if (keyboard_dropped() != dropped) {
            dropped = keyboard_dropped();
            put("\nAviso: eventos de teclado descartados: "); number(dropped, true); put("\n");
            redraw(line, cursor);
        }
        if (!event.pressed || (event.modifiers & KEY_ALT)) continue;
        if (event.modifiers & KEY_CTRL) {
            if (event.keycode == 0x2e) {
                put("^C\n"); used = cursor = 0; line[0] = 0; navigation = history_count;
            } else if (event.keycode == 0x16) {
                used = cursor = 0; line[0] = 0; navigation = history_count;
            }
            else if (event.keycode == 0x26) clear_screen();
            else if (event.keycode == 0x1e) cursor = 0;
            else if (event.keycode == 0x12) cursor = used;
            redraw(line, cursor); limit_warned = false;
            continue;
        }
        if (event.keycode == 0x148 || event.keycode == 0x150) {
            if (event.keycode == 0x148 && navigation > 0) {
                if (navigation == history_count) copy(draft, line);
                copy(line, history[history_slot(--navigation)]);
            } else if (event.keycode == 0x150 && navigation < history_count) {
                ++navigation;
                copy(line, navigation == history_count ? draft : history[history_slot(navigation)]);
            }
            used = cursor = length(line); limit_warned = false;
        } else if (event.keycode == 0x14b) { if (cursor) --cursor; }
        else if (event.keycode == 0x14d) { if (cursor < used) ++cursor; }
        else if (event.keycode == 0x147) cursor = 0;
        else if (event.keycode == 0x14f) cursor = used;
        else if (event.character == '\n') {
            put("\n"); command(line, bootloader, boot_version);
            used = cursor = 0; line[0] = 0; navigation = history_count; limit_warned = false;
        } else if (event.character == '\b' || event.keycode == 0x153) {
            if (event.character == '\b' && cursor) {
                --cursor; for (unsigned int i = cursor; i < used; ++i) line[i] = line[i + 1]; --used;
            } else if (event.keycode == 0x153 && cursor < used) {
                for (unsigned int i = cursor; i < used; ++i) line[i] = line[i + 1];
                --used;
            }
            navigation = history_count; limit_warned = false;
        } else if (event.character == '\t' || (event.character >= 32 && event.character <= 126)) {
            unsigned int count = event.character == '\t' ? 4 : 1;
            for (unsigned int j = 0; j < count; ++j) {
                if (used + 1 >= LINE_SIZE) {
                    if (!limit_warned) { put("\nLimite de 127 caracteres por linha.\n"); limit_warned = true; }
                    break;
                }
                for (unsigned int i = used + 1; i > cursor; --i) line[i] = line[i - 1];
                line[cursor++] = event.character == '\t' ? ' ' : event.character; ++used;
            }
            navigation = history_count;
        }
        redraw(line, cursor);
    }
}
