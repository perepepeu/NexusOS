#include <syscall.h>
#define CHECK(expression, status) do { if (!(expression)) sys_exit(status); } while (0)
__attribute__((noreturn)) static void interactive(void) {
    static const char prompt[] = "Syscalls Ring 3: digite uma linha (ate 64 bytes).\n";
    (void)sys_write(1, prompt, sizeof prompt - 1);
    char line[64]; size_t used = 0;
    while (used < sizeof line) {
        CHECK(sys_read(0, line + used, 1) == 1, 190);
        if (line[used++] == '\n') break;
    }
    static const char prefix[] = "Ring 3 recebeu: ";
    (void)sys_write(1, prefix, sizeof prefix - 1);
    (void)sys_write(1, line, used);
    if (!used || line[used - 1] != '\n') (void)sys_write(1, "\n", 1);
    CHECK(sys_getpid() > 1 && sys_sleep(20) == 0 && sys_yield() == 0, 191);
    sys_exit(0);
}
__attribute__((noreturn, section(".text.entry"))) void _start(uint64_t mode) {
    if (!mode) interactive();
    if (mode == 2) {
        (void)sys_read(0, (void *)0x600000, 1);
        sys_exit(199); /* O pai deve encerrar enquanto esta bloqueado. */
    }
    CHECK(sys_read(1, 0, 0) == -USER_EBADF && sys_read(2, 0, 1) == -USER_EBADF, 131);
    CHECK(sys_read((uint64_t)-1, 0, 0) == -USER_EBADF, 132);
    CHECK(sys_read(0, 0, USER_IO_MAX + 1) == -USER_EINVAL, 133);
    CHECK(sys_read(0, 0, (size_t)-1) == -USER_EINVAL, 134);
    CHECK(sys_read(0, 0, 0) == 0 && sys_read(0, (void *)(uintptr_t)-1, 0) == 0, 135);
    CHECK(sys_read(0, 0, 1) == -USER_EFAULT, 136);
    CHECK(sys_read(0, (void *)0xffffffff80000000ull, 1) == -USER_EFAULT, 137);
    CHECK(sys_read(0, (void *)0x0100000000000000ull, 1) == -USER_EFAULT, 138);
    CHECK(sys_read(0, (void *)(uintptr_t)-8, 16) == -USER_EFAULT, 139);
    static const char ro[] = "READ-RO-CODE";
    CHECK(sys_read(0, (void *)ro, 1) == -USER_EFAULT, 140);
    char *edge = (char *)0x600fff; *edge = 0x5a;
    CHECK(sys_read(0, edge, 2) == -USER_EFAULT && *edge == 0x5a, 141);
    char *cross = (char *)0x801fff;
    CHECK(sys_read(0, cross, 1) == 1 && cross[0] == 'a', 142);
    /* Com entrada pendente, falhas nao podem consumir bytes nem alterar dados. */
    CHECK(sys_read(0, edge, 2) == -USER_EFAULT && *edge == 0x5a, 143);
    CHECK(sys_read(0, (void *)ro, 1) == -USER_EFAULT, 144);
    size_t done = 1;
    while (done < 4) {
        int64_t count = sys_read(0, cross + done, 4 - done);
        CHECK(count > 0 && count <= (int64_t)(4 - done), 145);
        done += (size_t)count;
    }
    CHECK(cross[0] == 'a' && cross[1] == 'b' && cross[2] == 'c' && cross[3] == '\n', 146);
    (void)sys_write(1, "SYS-READ:", 9); CHECK(sys_write(1, cross, 4) == 4, 147);
    CHECK(sys_sleep(20) == 0, 148); /* Testemunha observa saida/reentrada em INPUT. */
    char *page = (char *)0x600000;
    CHECK(sys_read(0, page, USER_IO_MAX) == 1 && page[0] == 'z', 149);
    (void)sys_write(1, "SYS-READ-MAX:z\n", 15);
    (void)sys_write(1, "SYS-READ-OK\n", 12);
    sys_exit(0);
}
