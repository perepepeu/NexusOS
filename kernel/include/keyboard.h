#ifndef NEXUS_KEYBOARD_H
#define NEXUS_KEYBOARD_H
#include <stdbool.h>
#include <stdint.h>

enum key_modifier { KEY_SHIFT = 1, KEY_CTRL = 2, KEY_ALT = 4, KEY_CAPS = 8 };
/* Keycode: scancode set 1 sem break bit, com bit 8 para prefixo E0. */
struct key_event {
    uint16_t keycode;
    uint8_t modifiers;
    char character;
    bool pressed;
};

bool keyboard_init(void);
bool keyboard_ready(void);
void keyboard_irq(void);
bool keyboard_read(struct key_event *event);
void keyboard_wait(struct key_event *event);
unsigned int keyboard_dropped(void);
#endif
