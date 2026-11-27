#include <stddef.h>

/* Auxiliar C freestanding usado pelo compilador em agregados grandes.
 * Stores volatile impedem transformar esta rotina numa chamada a si mesma. */
void *memset(void *destination, int value, size_t count) {
    volatile unsigned char *bytes = destination;
    for (size_t i = 0; i < count; ++i) bytes[i] = (unsigned char)value;
    return destination;
}
