#include <stdint.h>
#include <limine.h>
#include <console.h>
#include <log.h>
#include <panic.h>

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

    if (boot_option("selftest=logs")) {
        log_debug("test", "DEBUG oculto");
        log_set_level(LOG_DEBUG);
        log_debug("test", "DEBUG habilitado");
        log_info("test", "Formato: %s %u %x %p %% %s", "ok", 42u, 0x2au,
            (void *)(uintptr_t)0x1234, (const char *)0);
        log_warn("test", "Aviso simulado; inicializacao continua.");
        log_error("test", "Erro recuperavel simulado; inicializacao continua.");
        log_set_level(LOG_INFO);
    }
    if (boot_option("selftest=panic")) {
        log_warn("test", "Injetando erro fatal para validar o diagnostico.");
        PANIC(ERR_SELFTEST, "Falha fatal simulada.");
    }
    if (boot_option("selftest=assert")) KASSERT(false);

    log_info("boot", "NexusOS: kernel inicial pronto. CPU em espera.");
    kernel_halt();
}
