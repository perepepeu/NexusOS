#include <syscall.h>
#define CHECK(expression, status) do { if (!(expression)) sys_exit(status); } while (0)
extern int user_abi_register_test(void);
static uint64_t ticks(void) { return (uint64_t)nexus_syscall3(USER_SYSCALL_TICKS, 0, 0, 0); }
__attribute__((noreturn, section(".text.entry"))) void _start(uint64_t kernel_target) {
    CHECK(sys_getpid() > 1 && sys_getpid() == sys_getpid(), 101);
    CHECK(nexus_syscall3(USER_SYSCALL_COUNT, 0, 0, 0) == -USER_ENOSYS, 102);
    CHECK(nexus_syscall3((uint64_t)-1, 0, 0, 0) == -USER_ENOSYS, 103);
    CHECK(sys_write(0, 0, 0) == -USER_EBADF && sys_write(3, 0, 1) == -USER_EBADF, 104);
    CHECK(sys_write((uint64_t)-1, 0, 0) == -USER_EBADF, 105);
    CHECK(sys_write(1, 0, USER_IO_MAX + 1) == -USER_EINVAL, 106);
    CHECK(sys_write(1, 0, (size_t)-1) == -USER_EINVAL, 107);
    CHECK(sys_write(1, 0, 0) == 0 && sys_write(2, (void *)(uintptr_t)-1, 0) == 0, 108);
    CHECK(sys_write(1, 0, 1) == -USER_EFAULT, 109);
    CHECK(sys_write(1, (void *)(uintptr_t)kernel_target, 8) == -USER_EFAULT, 110);
    CHECK(sys_write(1, (void *)0x0100000000000000ull, 1) == -USER_EFAULT, 111);
    CHECK(sys_write(1, (void *)(uintptr_t)-8, 16) == -USER_EFAULT, 112);
    CHECK(sys_write(1, (void *)0x500000, 1) == -USER_EFAULT, 113);
    CHECK(sys_write(1, (void *)0x600000, USER_IO_MAX) == USER_IO_MAX, 114);
    static const char forbidden[] = "FORBIDDEN-PARTIAL";
    char *tail = (char *)(0x601000 - (sizeof forbidden - 1));
    for (size_t i = 0; i < sizeof forbidden - 1; ++i) tail[i] = forbidden[i];
    CHECK(sys_write(1, tail, sizeof forbidden) == -USER_EFAULT, 115);
    static const char ro[] = "\nSYS-RO-TEXT\n";
    CHECK(sys_write(1, ro, sizeof ro - 1) == sizeof ro - 1, 116);
    static const char err[] = "SYS-STDERR\n";
    CHECK(sys_write(2, err, sizeof err - 1) == sizeof err - 1, 117);
    const char binary[] = {'S','Y','S','-','B','I','N',':',0,'Z','\n'};
    CHECK(sys_write(1, binary, sizeof binary) == sizeof binary, 118);
    /* Duas paginas de stack mapeadas: uma transferencia valida cruza o limite. */
    char *cross = (char *)0x801ffd;
    cross[0] = 'X'; cross[1] = '-'; cross[2] = 'P'; cross[3] = 'A'; cross[4] = 'G'; cross[5] = 'E'; cross[6] = '\n';
    CHECK(sys_write(1, cross, 7) == 7, 119);
    CHECK(sys_sleep((uint64_t)-1) == -USER_EINVAL, 120);
    CHECK(sys_yield() == 0 && sys_sleep(0) == 0, 121);
    uint64_t start = ticks(); CHECK(sys_sleep(1) == 0 && ticks() - start >= 2, 122);
    start = ticks(); CHECK(sys_sleep(21) == 0 && ticks() - start >= 4, 123);
    CHECK(user_abi_register_test() == 1, 124);
    static const char done[] = "SYS-WRITE-SLEEP-ABI-OK\n";
    CHECK(sys_write(1, done, sizeof done - 1) == sizeof done - 1, 125);
    sys_exit(0);
}
