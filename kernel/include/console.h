#ifndef NEXUS_CONSOLE_H
#define NEXUS_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <limine.h>

bool console_init(struct limine_framebuffer_response *response);
void console_write(const char *text);
void console_write_n(const char *text, size_t bytes);
void console_clear(void);
void console_input(const char *prompt, const char *line, unsigned int cursor);

#endif
