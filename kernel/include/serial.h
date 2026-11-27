#ifndef NEXUS_SERIAL_H
#define NEXUS_SERIAL_H
#include <stddef.h>
void serial_init(void);
void serial_write(const char *message);
void serial_write_n(const char *message, size_t bytes);
#endif
