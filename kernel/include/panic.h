#ifndef NEXUS_PANIC_H
#define NEXUS_PANIC_H
#include <stdint.h>

enum kernel_error {
    ERR_BOOT_PROTOCOL = 1,
    ERR_BOOT_INFO = 2,
    ERR_SELFTEST = 3,
    ERR_ASSERTION = 4,
    ERR_CPU_EXCEPTION = 5,
    ERR_MEMORY_MAP = 6,
    ERR_PAGE_ALLOCATOR = 7,
    ERR_VIRTUAL_MEMORY = 8,
};

struct panic_context {
    uint64_t vector, error, rip, rsp, rflags, cr2;
};
__attribute__((noreturn)) void kernel_exception(const char *name,
    const struct panic_context *context);

__attribute__((noreturn)) void kernel_halt(void);
__attribute__((noreturn)) void kernel_panic(enum kernel_error code,
    const char *message, const char *file, unsigned int line);

#define PANIC(code, message) kernel_panic(code, message, __FILE__, __LINE__)
#define KASSERT(expression) do { \
    if (!(expression)) PANIC(ERR_ASSERTION, "Assert falhou: " #expression); \
} while (0)

#endif
