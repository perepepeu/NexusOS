#ifndef NEXUS_CONSOLE_H
#define NEXUS_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <limine.h>

bool console_init(struct limine_framebuffer_response *response);
struct console_info { uint64_t width, height, pitch; uint16_t bpp; uint8_t rm,rs,gm,gs,bm,bs; };
bool console_info(struct console_info *out);
void console_write(const char *text);
void console_write_n(const char *text, size_t bytes);
void console_clear(void);
void console_input(const char *prompt, const char *line, unsigned int cursor);

#endif
