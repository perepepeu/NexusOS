#include <log.h>
#include <panic.h>
#include <serial.h>
#include <console.h>

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

__attribute__((noreturn)) void kernel_exception(const char *name,
        const struct panic_context *context) {
    panic_begin();
    log_write(LOG_FATAL, "panic", "KERNEL PANIC codigo=%u: %s", (unsigned int)ERR_CPU_EXCEPTION, name);
    log_write(LOG_FATAL, "cpu", "Vetor=%u Erro=%p", (unsigned int)context->vector,
        (void *)(uintptr_t)context->error);
    log_write(LOG_FATAL, "cpu", "RIP=%p RSP=%p", (void *)(uintptr_t)context->rip,
        (void *)(uintptr_t)context->rsp);
    log_write(LOG_FATAL, "cpu", "RFLAGS=%p", (void *)(uintptr_t)context->rflags);
    if (context->vector == 14)
        log_write(LOG_FATAL, "cpu", "CR2=%p", (void *)(uintptr_t)context->cr2);
    if (context->vector == 8)
        log_write(LOG_FATAL, "cpu", "IST double fault ativa.");
    panic_end();
}
