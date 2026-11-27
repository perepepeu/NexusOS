#include <log.h>
#include <panic.h>
#include <serial.h>
#include <console.h>

static volatile unsigned int panicking;

__attribute__((noreturn)) void kernel_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt" : : : "memory");
}

__attribute__((noreturn)) void kernel_panic(enum kernel_error code,
        const char *message, const char *file, unsigned int line) {
    __asm__ volatile ("cli" : : : "memory");
    if (panicking) {
        serial_write("FATAL: panic recursivo. CPU interrompida.\n");
        kernel_halt();
    }
    panicking = 1;
    console_clear();
    console_write("NexusOS - erro fatal\n\n");
    log_write(LOG_FATAL, "panic", "KERNEL PANIC codigo=%u: %s", (unsigned int)code, message);
    log_write(LOG_FATAL, "panic", "Origem: %s:%u", file, line);
    log_write(LOG_FATAL, "panic", "Sistema interrompido. Reinicie a maquina.");
    kernel_halt();
}
