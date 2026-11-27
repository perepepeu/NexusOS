#include <io.h>
#include <serial.h>

void serial_init(void) {
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x80);
    outb(0x3f8, 0x01);  /* COM1, 115200 baud, 8N1, polling. */
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}

static void putc(char character) {
    for (unsigned int retries = 0; retries < 100000; ++retries) {
        if (inb(0x3fd) & 0x20) {
            outb(0x3f8, (uint8_t)character);
            return;
        }
    }
}

void serial_write(const char *message) {
    uint64_t flags = irq_save();
    while (*message) {
        if (*message == '\n') putc('\r');
        putc(*message++);
    }
    irq_restore(flags);
}
