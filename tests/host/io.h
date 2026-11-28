#ifndef NEXUS_TEST_IO_H
#define NEXUS_TEST_IO_H
#include <stdint.h>
static inline uint64_t irq_save(void) { return 1u << 9; }
static inline void irq_restore(uint64_t flags) { (void)flags; }
#endif
