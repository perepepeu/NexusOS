#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <console.h>
#include <log.h>
#include <serial.h>

static enum log_level minimum_level = LOG_INFO;
static unsigned int sequence;

static void write(const char *text) {
    serial_write(text);
    console_write(text);
}

static void character(char value) {
    char text[2] = {value, 0};
    write(text);
}

static void number(uint64_t value, unsigned int base) {
    const char digits[] = "0123456789abcdef";
    char buffer[21];
    size_t used = 0;
    do {
        buffer[used++] = digits[value % base];
        value /= base;
    } while (value);
    while (used) character(buffer[--used]);
}

void log_init(void) {
    serial_init();
    minimum_level = LOG_INFO;
    sequence = 0;
}

void log_set_level(enum log_level minimum) {
    if (minimum >= LOG_DEBUG && minimum <= LOG_FATAL) minimum_level = minimum;
}

void log_write(enum log_level level, const char *module, const char *format, ...) {
    static const char *const names[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    if (level < LOG_DEBUG || level > LOG_FATAL || level < minimum_level) return;
    write("["); number(++sequence, 10); write("] [");
    write(names[level]); write("] ["); write(module ? module : "kernel"); write("] ");
    va_list arguments;
    va_start(arguments, format);
    if (!format) format = "(null)";
    while (*format) {
        if (*format != '%') { character(*format++); continue; }
        ++format;
        switch (*format) {
            case 's': {
                const char *text = va_arg(arguments, const char *);
                write(text ? text : "(null)");
                break;
            }
            case 'u': number(va_arg(arguments, unsigned int), 10); break;
            case 'x': number(va_arg(arguments, unsigned int), 16); break;
            case 'p':
                write("0x"); number((uintptr_t)va_arg(arguments, void *), 16); break;
            case '%': character('%'); break;
            case '\0': character('%'); goto done;
            default: character('%'); character(*format); break;
        }
        ++format;
    }
done:
    va_end(arguments);
    write("\n");
}
