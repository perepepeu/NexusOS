#include <keyboard.h>
#include <interrupts.h>
#include <io.h>
#include <log.h>
#include <panic.h>

enum { QUEUE_SIZE = 64, WAIT_LIMIT = 1000000 };
static struct key_event queue[QUEUE_SIZE];
static volatile unsigned int head, tail, dropped;
static bool left_shift, right_shift, left_ctrl, right_ctrl, left_alt, right_alt;
static bool caps, caps_held, extended;
static unsigned int pause_bytes;

static const char normal[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',
    [0x0c]='-',[0x0d]='=',[0x0e]='\b',[0x0f]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1a]='[',[0x1b]=']',[0x1c]='\n',
    [0x1e]='a',[0x1f]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',
    [0x28]='\'',[0x29]='`',[0x2b]='\\',
    [0x2c]='z',[0x2d]='x',[0x2e]='c',[0x2f]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',
    [0x37]='*',[0x39]=' ',[0x4a]='-',[0x4e]='+',
};
static const char shifted[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0a]='(',[0x0b]=')',
    [0x0c]='_',[0x0d]='+',[0x1a]='{',[0x1b]='}',[0x27]=':',
    [0x28]='"',[0x29]='~',[0x2b]='|',[0x33]='<',[0x34]='>',[0x35]='?',
};

static bool wait_input(void) {
    for (unsigned int i = 0; i < WAIT_LIMIT; ++i) {
        if (!(inb(0x64) & 2)) return true;
        __asm__ volatile ("pause");
    }
    return false;
}
static bool command(uint8_t value) {
    if (!wait_input()) return false;
    outb(0x64, value);
    return true;
}
static bool data(uint8_t value) {
    if (!wait_input()) return false;
    outb(0x60, value);
    return true;
}
static bool receive(uint8_t *value) {
    for (unsigned int i = 0; i < WAIT_LIMIT; ++i) {
        uint8_t status = inb(0x64);
        if (status & 1) {
            uint8_t byte = inb(0x60);
            if (status & 0xc0) return false;
            if (status & 0x20) continue;
            *value = byte;
            return true;
        }
        __asm__ volatile ("pause");
    }
    return false;
}
static bool device_command(uint8_t value) {
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        if (!data(value)) return false;
        for (unsigned int i = 0; i < 16; ++i) {
            uint8_t reply;
            if (!receive(&reply)) return false;
            if (reply == 0xfa) return true;
            if (reply == 0xfe) break;
            if (reply == 0xfc || reply == 0xfd) return false;
            /* Descarta teclas que ja estavam em transito antes do ACK. */
        }
    }
    return false;
}

bool keyboard_init(void) {
    uint64_t flags = irq_save();
    interrupts_disable_irq(1);
    head = tail = dropped = 0;
    left_shift = right_shift = left_ctrl = right_ctrl = left_alt = right_alt = false;
    caps = caps_held = extended = false;
    pause_bytes = 0;
    uint8_t config = 0;
    bool ok = command(0xad) && command(0xa7);
    for (unsigned int i = 0; i < 64 && (inb(0x64) & 1); ++i) (void)inb(0x60);
    ok = ok && command(0x20) && receive(&config);
    /* IRQs desligadas durante comandos; tradutor set 2 -> set 1 habilitado. */
    config = (config | 0x40) & ~0x03;
    ok = ok && command(0x60) && data(config) && command(0xae)
        && device_command(0xf5) && device_command(0xf0) && device_command(0x02)
        && device_command(0xf4);
    config = (config | 1) & ~0x10;
    ok = ok && command(0x60) && data(config);
    if (ok) interrupts_enable_irq(1);
    irq_restore(flags);
    if (ok) log_info("keyboard", "PS/2 pronto: IRQ1, scancodes set 1, layout US.");
    else log_warn("keyboard", "PS/2 indisponivel ou sem ACK; IRQ1 permanece mascarada.");
    return ok;
}

static void decode(uint8_t byte) {
    if (pause_bytes) { --pause_bytes; return; }
    if (byte == 0xe1) { pause_bytes = 5; extended = false; return; }
    if (byte == 0xe0) { extended = true; return; }
    if (byte == 0xfa || byte == 0xfe) { extended = false; return; }
    bool ext = extended;
    extended = false;
    bool pressed = !(byte & 0x80);
    uint8_t code = byte & 0x7f;
    if (!code || code == 0x7f) return;
    /* Print Screen contem shifts falsos, que nao devem alterar modificadores. */
    if (ext && (code == 0x2a || code == 0x36 || code == 0x37)) return;
    if (code == 0x2a && !ext) left_shift = pressed;
    if (code == 0x36 && !ext) right_shift = pressed;
    if (code == 0x1d) { if (ext) right_ctrl = pressed; else left_ctrl = pressed; }
    if (code == 0x38) { if (ext) right_alt = pressed; else left_alt = pressed; }
    if (code == 0x3a && !ext) {
        if (pressed && !caps_held) caps = !caps;
        caps_held = pressed;
    }
    bool shift = left_shift || right_shift;
    uint8_t modifiers = (shift ? KEY_SHIFT : 0) | ((left_ctrl || right_ctrl) ? KEY_CTRL : 0)
        | ((left_alt || right_alt) ? KEY_ALT : 0) | (caps ? KEY_CAPS : 0);
    char character = 0;
    if (pressed) {
        character = ext ? (code == 0x1c ? '\n' : code == 0x35 ? '/' : 0) : normal[code];
        if (character >= 'a' && character <= 'z') {
            if (shift != caps) character -= 'a' - 'A';
        } else if (shift && !ext && shifted[code]) character = shifted[code];
    }
    unsigned int next = (head + 1) % QUEUE_SIZE;
    if (next == tail) { ++dropped; return; }
    queue[head] = (struct key_event){code | (ext ? 0x100 : 0), modifiers, character, pressed};
    __asm__ volatile ("" : : : "memory");
    head = next;
}

void keyboard_irq(void) {
    for (unsigned int i = 0; i < 32; ++i) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        uint8_t byte = inb(0x60);
        if (status & 0x20) continue;
        if (status & 0xc0) { extended = false; pause_bytes = 0; ++dropped; continue; }
        decode(byte);
    }
}

bool keyboard_read(struct key_event *event) {
    if (!event) return false;
    uint64_t flags = irq_save();
    bool available = head != tail;
    if (available) { *event = queue[tail]; tail = (tail + 1) % QUEUE_SIZE; }
    irq_restore(flags);
    return available;
}

void keyboard_wait(struct key_event *event) {
    KASSERT(event);
    for (;;) {
        uint64_t flags = irq_save();
        KASSERT(flags & (1u << 9));
        if (keyboard_read(event)) { irq_restore(flags); return; }
        /* STI e HLT adjacentes eliminam a janela de perda de wakeup. */
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
}

unsigned int keyboard_dropped(void) { return dropped; }
