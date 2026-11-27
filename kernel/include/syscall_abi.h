#ifndef NEXUS_SYSCALL_ABI_H
#define NEXUS_SYSCALL_ABI_H
/* ABI v1, x86_64: INT80, RAX=numero/retorno, RDI/RSI/RDX=argumentos.
 * GPRs exceto RAX e flags de usuario sao preservados. Erros: -errno.
 * Numeros 0..4 preservam os programas iniciais de Ring 3. */
#define USER_ABI_VERSION 1
#define USER_SYSCALL_EXIT 0
#define USER_SYSCALL_YIELD 1
#define USER_SYSCALL_GETPID 2
#define USER_SYSCALL_GETTID 3
#define USER_SYSCALL_TICKS 4
#define USER_SYSCALL_WRITE 5
#define USER_SYSCALL_READ 6
#define USER_SYSCALL_SLEEP 7
#define USER_SYSCALL_COUNT 8
#define USER_EBADF 9
#define USER_EFAULT 14
#define USER_ENODEV 19
#define USER_EINVAL 22
#define USER_ENOSYS 38
#define USER_IO_MAX 4096
#define USER_SLEEP_MAX_MS 9223372036854775807
#endif
