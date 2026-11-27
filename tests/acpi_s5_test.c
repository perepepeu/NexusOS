#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <acpi_s5.h>

int main(void) {
    const uint8_t valid[] = {8,'_','S','5','_',0x12,6,2,0x0a,5,0x0a,6};
    uint8_t a = 0, b = 0;
    assert(acpi_find_s5(valid, sizeof valid, &a, &b) && a == 5 && b == 6);
    for (size_t n = 0; n < sizeof valid; ++n) {
        uint8_t *partial = malloc(n ? n : 1);
        memcpy(partial, valid, n);
        assert(!acpi_find_s5(partial, n, &a, &b));
        free(partial);
    }
    uint8_t scoped[16] = {0x10,15,'\\',0};
    memcpy(scoped + 4, valid, sizeof valid);
    assert(acpi_find_s5(scoped, sizeof scoped, &a, &b));
    uint8_t buffer[21] = {8,'B','U','F','0',0x11,15,0x0a,12};
    memcpy(buffer + 9, valid, sizeof valid);
    assert(!acpi_find_s5(buffer, sizeof buffer, &a, &b));
    uint8_t method[19] = {0x14,18,'T','E','S','T',0};
    memcpy(method + 7, valid, sizeof valid);
    assert(!acpi_find_s5(method, sizeof method, &a, &b));
    uint8_t combined[33];
    memcpy(combined, buffer, sizeof buffer);
    memcpy(combined + sizeof buffer, valid, sizeof valid);
    assert(acpi_find_s5(combined, sizeof combined, &a, &b));
    uint8_t invalid[sizeof valid];
    memcpy(invalid, valid, sizeof valid); invalid[9] = 8;
    assert(!acpi_find_s5(invalid, sizeof invalid, &a, &b));
    memcpy(invalid, valid, sizeof valid); invalid[7] = 3;
    assert(!acpi_find_s5(invalid, sizeof invalid, &a, &b));
    assert(!acpi_find_s5(0, 1, &a, &b));
    uint32_t random = 1;
    for (unsigned int trial = 0; trial < 10000; ++trial) {
        size_t size = trial % 128;
        uint8_t *bytes = malloc(size ? size : 1);
        for (size_t i = 0; i < size; ++i) {
            random = random * 1664525u + 1013904223u;
            bytes[i] = random >> 24;
        }
        (void)acpi_find_s5(bytes, size, &a, &b);
        free(bytes);
    }
    puts("OK: AML S5, truncamentos, falsos positivos e 10000 entradas aleatorias.");
    return 0;
}
