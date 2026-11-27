#ifndef NEXUS_CONSOLE_H
#define NEXUS_CONSOLE_H

#include <stdbool.h>
#include <limine.h>

bool console_init(struct limine_framebuffer_response *response);
void console_write(const char *text);

#endif
