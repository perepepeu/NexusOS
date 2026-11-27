#ifndef NEXUS_POWER_H
#define NEXUS_POWER_H
#include <stdbool.h>
#include <stdint.h>
#include <limine.h>
void power_init(const void *rsdp, uint64_t hhdm, const struct limine_memmap_response *map);
bool power_reboot(void);
bool power_shutdown(void);
#endif
