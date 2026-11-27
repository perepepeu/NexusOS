#include <stdint.h>
#include <limine.h>
#include <console.h>

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

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb(0x3f9, 0x00);  /* Desativa interrupcoes da COM1. */
    outb(0x3fb, 0x80);  /* Acesso ao divisor de baud rate. */
    outb(0x3f8, 0x01);  /* 115200 baud. */
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);  /* 8 bits, sem paridade, 1 stop bit. */
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}

static void serial_putc(char character) {
    /* Limita a espera para nao travar se a UART estiver indisponivel. */
    for (unsigned int retries = 0; retries < 100000; ++retries) {
        if (inb(0x3fd) & 0x20) {
            outb(0x3f8, (uint8_t)character);
            return;
        }
    }
}

static void serial_write(const char *message) {
    while (*message) {
        if (*message == '\n') {
            serial_putc('\r');
        }
        serial_putc(*message++);
    }
}

static void print(const char *message) {
    serial_write(message);
    console_write(message);
}

__attribute__((noreturn))
static void halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

__attribute__((noreturn))
void kmain(void) {
    serial_init();

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_write("PANIC: revisao do protocolo Limine nao suportada.\n");
        halt();
    }
    if (console_init(framebuffer.response)) {
        print("NexusOS\n\nTela: framebuffer pronto.\n");
    } else {
        serial_write("AVISO: framebuffer RGB de 32 bits indisponivel; usando serial.\n");
    }
    print("NexusOS: iniciando kernel x86_64...\n");
    struct limine_bootloader_info_response *info = bootloader_info.response;
    if (info == 0 || info->name == 0 || info->version == 0) {
        print("PANIC: resposta de identificacao do bootloader ausente.\n");
        halt();
    }

    print("Bootloader: ");
    print(info->name);
    print(" ");
    print(info->version);
    print("\nProtocolo Limine: OK\n");
    print("NexusOS: kernel inicial pronto. CPU em espera.\n");
    halt();
}
