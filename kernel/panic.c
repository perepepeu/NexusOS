#include <log.h>
#include <panic.h>
#include <serial.h>
#include <console.h>
#include <heap.h>
#include <process.h>

static volatile unsigned int panicking;

__attribute__((noreturn)) void kernel_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt" : : : "memory");
}

static void panic_begin(void) {
    __asm__ volatile ("cli" : : : "memory");
    if (panicking) {
        serial_write("FATAL: panic recursivo. CPU interrompida.\n");
        kernel_halt();
    }
    panicking = 1;
    console_clear();
    console_write("NexusOS - erro fatal\n\n");
}

__attribute__((noreturn)) static void panic_end(void) {
    log_write(LOG_FATAL, "panic", "Sistema interrompido. Reinicie a maquina.");
    kernel_halt();
}

__attribute__((noreturn)) void kernel_panic(enum kernel_error code,
        const char *message, const char *file, unsigned int line) {
    panic_begin();
    log_write(LOG_FATAL, "panic", "KERNEL PANIC codigo=%u: %s", (unsigned int)code, message);
    log_write(LOG_FATAL, "panic", "Origem: %s:%u", file, line);
    panic_end();
}

__attribute__((noreturn)) void kernel_memory_panic(const char *message,
        uint64_t address, const char *file, unsigned int line) {
    panic_begin();
    log_write(LOG_FATAL, "panic", "KERNEL PANIC codigo=%u: %s", (unsigned int)ERR_MEMORY_ACCESS, message);
    log_write(LOG_FATAL, "memory", "Endereco detectado=%p", (void *)(uintptr_t)address);
    log_write(LOG_FATAL, "panic", "Origem: %s:%u", file, line);
    panic_end();
}

__attribute__((noreturn)) void kernel_exception(const char *name,
        const struct panic_context *context) {
    panic_begin();
    log_write(LOG_FATAL, "panic", "KERNEL PANIC codigo=%u: %s", (unsigned int)ERR_CPU_EXCEPTION, name);
    log_write(LOG_FATAL, "cpu", "Vetor=%u Erro=%p", (unsigned int)context->vector,
        (void *)(uintptr_t)context->error);
    log_write(LOG_FATAL, "cpu", "RIP=%p RSP=%p", (void *)(uintptr_t)context->rip,
        (void *)(uintptr_t)context->rsp);
    log_write(LOG_FATAL, "cpu", "RFLAGS=%p", (void *)(uintptr_t)context->rflags);
    log_write(LOG_FATAL, "cpu", "PID=%llu", (unsigned long long)process_current_pid());
    if (context->vector == 14) {
        log_write(LOG_FATAL, "cpu", "CR2=%p", (void *)(uintptr_t)context->cr2);
        uint64_t error = context->error;
        log_write(LOG_FATAL, "memory", "Acesso=%s Modo=%s Causa=%s",
            (error & 16) ? "execucao" : (error & 2) ? "escrita" : "leitura",
            (error & 4) ? "usuario" : "supervisor",
            (error & 1) ? "protecao violada" : "pagina ausente");
        log_write(LOG_FATAL, "memory", "RSVD=%u PK=%u SS=%u SGX=%u",
            (unsigned int)((error >> 3) & 1), (unsigned int)((error >> 5) & 1),
            (unsigned int)((error >> 6) & 1), (unsigned int)((error >> 15) & 1));
        log_write(LOG_FATAL, "memory", "Regiao: %s", context->cr2 < 4096 ? "pagina nula" : heap_address_kind(context->cr2));
    }
    if (context->vector == 8)
        log_write(LOG_FATAL, "cpu", "IST double fault ativa.");
    panic_end();
}
