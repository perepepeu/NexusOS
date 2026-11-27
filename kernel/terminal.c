#include <stdbool.h>
#include <stdint.h>
#include <console.h>
#include <interrupts.h>
#include <keyboard.h>
#include <log.h>
#include <serial.h>
#include <terminal.h>
#include <version.h>

enum { LINE_SIZE = 128, HISTORY_SIZE = 8 };
static const char prompt[] = "nexus> ";
static char history[HISTORY_SIZE][LINE_SIZE];
static unsigned int history_head, history_count;

static void put(const char *text) { console_write(text); serial_write(text); }

static void number(uint64_t value, bool both) {
    char digits[21], text[2] = {0, 0};
    unsigned int count = 0;
    do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
    while (count) { text[0] = digits[--count]; if (both) put(text); else serial_write(text); }
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
    console_input(prompt, line, cursor);
    serial_write("\r\x1b[2K"); serial_write(prompt); serial_write(line);
    serial_write("\r\x1b["); number(sizeof prompt - 1 + cursor, false); serial_write("C");
}

static void clear_screen(void) {
    console_clear();
    serial_write("\x1b[2J\x1b[H");
}

static unsigned int history_slot(unsigned int position) {
    return (history_head + HISTORY_SIZE - history_count + position) % HISTORY_SIZE;
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
    bool known = equal(name, "help") || equal(name, "clear") || equal(name, "version")
        || equal(name, "uptime") || equal(name, "sysinfo") || equal(name, "history");
    if (!known) { put("Comando desconhecido: "); put(name); put(". Digite help.\n"); return; }
    if (*args) { put("Uso: "); put(name); put(" (sem argumentos).\n"); return; }
    if (equal(name, "help")) {
        put("Comandos: help clear echo version uptime sysinfo history\n");
        put("Edicao: setas, Home/End, Delete, Backspace, historico Up/Down.\n");
        put("Ctrl+C cancela, Ctrl+U apaga a linha, Ctrl+L limpa a tela.\n");
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
