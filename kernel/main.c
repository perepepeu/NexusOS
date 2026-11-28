#include <stdint.h>
#include <limine.h>
#include <console.h>
#include <log.h>
#include <panic.h>
#include <interrupts.h>
#include <keyboard.h>
#include <serial.h>
#include <terminal.h>
#include <power.h>
#include <memory.h>
#include <pmm.h>
#include <vmm.h>
#include <heap.h>
#include <process.h>
#include <user_mode.h>
#include <programs.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_bootloader_info_request bootloader_info = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_cmdline_request command_line = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm = {.id = LIMINE_HHDM_REQUEST_ID};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request executable_address = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp = {.id = LIMINE_RSDP_REQUEST_ID};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map = {.id = LIMINE_MEMMAP_REQUEST_ID};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request modules = {.id = LIMINE_MODULE_REQUEST_ID};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static bool boot_option(const char *option) {
    if (!command_line.response || !command_line.response->cmdline) return false;
    const char *text = command_line.response->cmdline;
    while (*text) {
        while (*text == ' ' || *text == '\t') ++text;
        const char *start = text;
        while (*text && *text != ' ' && *text != '\t') ++text;
        const char *expected = option;
        while (start < text && *expected && *start == *expected) { ++start; ++expected; }
        if (start == text && !*expected) return true;
    }
    return false;
}

static void echo(const char *text) {
    console_write(text);
    serial_write(text);
}

__attribute__((noreturn)) static void keyboard_loop(bool trace) {
    char line[128];
    unsigned int length = 0, previous_dropped = 0;
    log_info("keyboard", "Teclado pronto. Digite e pressione Enter.");
    echo("teclado> ");
    for (;;) {
        struct key_event event;
        keyboard_wait(&event);
        if (trace) log_info("key", "code=%x down=%u mods=%x char=%u", (unsigned int)event.keycode,
            (unsigned int)event.pressed, (unsigned int)event.modifiers,
            (unsigned int)(uint8_t)event.character);
        unsigned int lost = keyboard_dropped();
        if (lost != previous_dropped) {
            log_warn("keyboard", "Descartes acumulados: %u", lost);
            previous_dropped = lost;
        }
        if (!event.pressed || (event.modifiers & (KEY_CTRL | KEY_ALT))) continue;
        char c = event.character;
        if (c == '\n') {
            line[length] = 0;
            echo("\n");
            log_info("keyboard", "Entrada: %s", line);
            length = 0;
            echo("teclado> ");
        } else if (c == '\b') {
            if (length) {
                --length;
                console_write("\b");
                serial_write("\b \b");
            }
        } else if (c == '\t') {
            for (unsigned int i = 0; i < 4 && length < sizeof line - 1; ++i) {
                line[length++] = ' '; echo(" ");
            }
        } else if (c >= 32 && c <= 126 && length < sizeof line - 1) {
            line[length++] = c;
            char text[2] = {c, 0};
            echo(text);
        }
    }
}

__attribute__((noreturn))
void kmain(void) {
    log_init();
    log_info("boot", "NexusOS: iniciando kernel x86_64...");

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        PANIC(ERR_BOOT_PROTOCOL, "Revisao do protocolo Limine nao suportada.");
    }
    if (console_init(framebuffer.response)) {
        console_write("NexusOS\n\n");
        log_info("video", "Tela: framebuffer pronto.");
    } else {
        log_warn("video", "Framebuffer RGB de 32 bits indisponivel; usando serial.");
    }
    struct limine_bootloader_info_response *info = bootloader_info.response;
    if (info == 0 || info->name == 0 || info->version == 0) {
        PANIC(ERR_BOOT_INFO, "Resposta de identificacao do bootloader ausente.");
    }

    log_info("boot", "Bootloader: %s %s", info->name, info->version);
    log_info("boot", "Protocolo Limine: OK");
    if (boot_option("log=debug")) log_set_level(LOG_DEBUG);
    log_debug("boot", "Diagnostico detalhado habilitado.");
    KASSERT(hhdm.response && executable_address.response);
    interrupts_init(hhdm.response->offset, executable_address.response->physical_base,
        executable_address.response->virtual_base);
    timer_wait(3);
    log_info("irq", "Timer IRQ0 confirmado: ticks=%u", (unsigned int)timer_ticks());
    enum memory_error memory_error = memory_detect(memory_map.response);
    if (memory_error != MEMORY_OK) PANIC(ERR_MEMORY_MAP, memory_error_name(memory_error));
    const struct memory_stats *ram = memory_get_stats();
    log_info("memory", "Mapa fisico: %u regioes. Utilizavel: %llu bytes; paginas 4KiB: %llu.",
        ram->region_count, (unsigned long long)ram->bytes[MEMORY_USABLE], (unsigned long long)ram->usable_pages);
    if (pmm_init(hhdm.response->offset) != PMM_OK)
        PANIC(ERR_PAGE_ALLOCATOR, "Nao foi possivel iniciar o alocador de paginas.");
    struct pmm_stats pages;
    pmm_get_stats(&pages);
    log_info("pmm", "Alocador pronto: %llu paginas livres; metadados=%llu paginas.",
        (unsigned long long)pages.free_pages, (unsigned long long)pages.metadata_pages);
    if (boot_option("selftest=pmm")) pmm_selftest(hhdm.response->offset);
    if (vmm_init(hhdm.response->offset) != VMM_OK)
        PANIC(ERR_VIRTUAL_MEMORY, "Nao foi possivel iniciar a memoria virtual.");
    struct vmm_stats virtual_memory;
    vmm_get_stats(&virtual_memory);
    log_info("vmm", "VMM pronto: CR3=%p janela=%p. NX e WP habilitados.",
        (void *)(uintptr_t)virtual_memory.root_address, (void *)(uintptr_t)virtual_memory.window_base);
    if (boot_option("selftest=vmm")) vmm_selftest(hhdm.response->offset, 0);
    if (boot_option("selftest=vmm-unmapped")) vmm_selftest(hhdm.response->offset, 1);
    if (boot_option("selftest=vmm-readonly")) vmm_selftest(hhdm.response->offset, 2);
    if (boot_option("selftest=vmm-nx")) vmm_selftest(hhdm.response->offset, 3);
    if (!heap_init()) PANIC(ERR_HEAP, "Nao foi possivel iniciar o heap do kernel.");
    log_info("heap", "Heap pronto: alinhamento 16 bytes; limite virtual 64 MiB, sob demanda.");
    log_info("heap", "Protecao: guardas de arena, redzones e veneno de blocos livres ativos.");
    if (boot_option("selftest=heap")) heap_selftest();
    if (boot_option("selftest=access-null")) heap_access_selftest(1);
    if (boot_option("selftest=access-guard-low")) heap_access_selftest(2);
    if (boot_option("selftest=access-guard-high")) heap_access_selftest(3);
    if (boot_option("selftest=access-unmapped")) heap_access_selftest(4);
    if (boot_option("selftest=access-overflow")) heap_access_selftest(5);
    if (boot_option("selftest=access-underflow")) heap_access_selftest(6);
    if (boot_option("selftest=access-freed")) heap_access_selftest(7);
    if (boot_option("selftest=access-doublefree")) heap_access_selftest(8);
    if (boot_option("selftest=access-badfree")) heap_access_selftest(9);
    if (boot_option("selftest=access-nx")) heap_access_selftest(10);

    if (boot_option("selftest=interrupts")) interrupts_selftest();
    if (boot_option("selftest=doublefault")) interrupts_test_double_fault();
    if (boot_option("selftest=divide")) {
        __asm__ volatile ("xor %%edx, %%edx; mov $1, %%eax; xor %%ecx, %%ecx; div %%ecx"
            : : : "rax", "rcx", "rdx", "cc");
        PANIC(ERR_SELFTEST, "Divisao por zero nao gerou excecao.");
    }
    if (boot_option("selftest=opcode")) __asm__ volatile ("ud2");
    if (boot_option("selftest=pagefault")) {
        (void)*(volatile uint64_t *)(uintptr_t)0x400000000000;
        PANIC(ERR_SELFTEST, "Endereco de teste deveria estar ausente.");
    }
    if (boot_option("selftest=protection")) {
        __asm__ volatile ("mov $0xfff8, %%ax; mov %%ax, %%ds" : : : "rax");
        PANIC(ERR_SELFTEST, "Seletor invalido nao gerou excecao.");
    }

    if (boot_option("selftest=logs")) {
        log_debug("test", "DEBUG oculto");
        log_set_level(LOG_DEBUG);
        log_debug("test", "DEBUG habilitado");
        log_info("test", "Formato: %s %u %x %p %% %s", "ok", 42u, 0x2au,
            (void *)(uintptr_t)0x1234, (const char *)0);
        log_info("test", "Inteiro 64 bits: %llu", 4294967303ull);
        log_warn("test", "Aviso simulado; inicializacao continua.");
        log_error("test", "Erro recuperavel simulado; inicializacao continua.");
        log_set_level(LOG_INFO);
    }
    if (boot_option("selftest=panic")) {
        log_warn("test", "Injetando erro fatal para validar o diagnostico.");
        PANIC(ERR_SELFTEST, "Falha fatal simulada.");
    }
    if (boot_option("selftest=assert")) KASSERT(false);

    if (!process_init()) PANIC(ERR_PROCESS, "Nao foi possivel iniciar o gerenciador de processos.");
    log_info("process", "Gerenciador pronto: PID 1, Idle PID 0; 32 PCBs/64 TCBs em ring 0.");
    log_info("sched", "Round Robin preemptivo: PIT 100 Hz, quantum 5 ticks (50 ms).");
    struct pmm_stats process_pages; struct vmm_stats process_maps;
    pmm_get_stats(&process_pages); vmm_get_stats(&process_maps);
    log_info("process", "Base: alocadas=%llu mapeadas=%llu tabelas=%llu",
        (unsigned long long)process_pages.allocated_pages, (unsigned long long)process_maps.mapped_pages,
        (unsigned long long)process_maps.table_pages);
    programs_init(modules.response);
    if (boot_option("selftest=elf")) elf_process_selftest();
    if (boot_option("selftest=processes")) process_selftest(false);
    if (boot_option("selftest=scheduler")) scheduler_selftest();
    if (boot_option("selftest=user")) user_mode_selftest();
    if (boot_option("selftest=user-faults")) user_mode_fault_selftest();
    if (boot_option("selftest=syscalls")) syscall_selftest();
    if (boot_option("selftest=process-guard")) process_selftest(true);
    log_info("boot", "NexusOS: kernel inicial pronto. CPU em espera.");
    power_init(rsdp.response && !boot_option("selftest=power-fallback") ? rsdp.response->address : 0,
        hhdm.response->offset, memory_map.response);
    if (keyboard_init()) {
        if (boot_option("selftest=sysread")) syscall_read_selftest();
        if (boot_option("selftest=keyboard") || boot_option("selftest=keyboard-echo"))
            keyboard_loop(boot_option("selftest=keyboard"));
        terminal_run(info->name, info->version);
    }
    for (;;) process_sleep(1);
}
