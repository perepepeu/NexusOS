#ifndef NEXUS_ACPI_S5_H
#define NEXUS_ACPI_S5_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool acpi_find_s5(const uint8_t *aml, size_t size, uint8_t *type_a, uint8_t *type_b);
#endif
