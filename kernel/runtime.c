#include <runtime.h>
#include <stdint.h>

/* Auxiliar C freestanding usado pelo compilador em agregados grandes.
 * Stores volatile impedem transformar esta rotina numa chamada a si mesma. */
void *memset(void *destination, int value, size_t count) {
    volatile unsigned char *bytes = destination;
    for (size_t i = 0; i < count; ++i) bytes[i] = (unsigned char)value;
    return destination;
}
void *memcpy(void *restrict destination, const void *restrict source, size_t count) {
    volatile unsigned char *d = destination; const volatile unsigned char *s = source;
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return destination;
}
void *memmove(void *destination, const void *source, size_t count) {
    volatile unsigned char *d = destination; const volatile unsigned char *s = source;
    if ((uintptr_t)d <= (uintptr_t)s || (uintptr_t)d - (uintptr_t)s >= count)
        for (size_t i = 0; i < count; ++i) d[i] = s[i];
    else for (size_t i = count; i; --i) d[i - 1] = s[i - 1];
    return destination;
}
int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}
size_t strlen(const char *text) { size_t n = 0; while (text[n]) ++n; return n; }
int strcmp(const char *a, const char *b) {
    while (*a && (unsigned char)*a == (unsigned char)*b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if ((unsigned char)a[i] != (unsigned char)b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) break;
    }
    return 0;
}
char *strcpy(char *restrict destination, const char *restrict source) {
    size_t i = 0; do { destination[i] = source[i]; } while (source[i++]); return destination;
}
char *strncpy(char *restrict destination, const char *restrict source, size_t count) {
    size_t i = 0;
    for (; i < count && source[i]; ++i) destination[i] = source[i];
    for (; i < count; ++i) destination[i] = 0;
    return destination;
}
