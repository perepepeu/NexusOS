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
#define USER_SYSCALL_OPEN 8
#define USER_SYSCALL_CLOSE 9
#define USER_SYSCALL_SEEK 10
#define USER_SYSCALL_STAT 11
#define USER_SYSCALL_MKDIR 12
#define USER_SYSCALL_UNLINK 13
#define USER_SYSCALL_READDIR 14
#define USER_SYSCALL_CHDIR 15
#define USER_SYSCALL_GETCWD 16
#define USER_SYSCALL_SYNC 17
#define USER_SYSCALL_COUNT 18
#define USER_ENOENT 2
#define USER_EIO 5
#define USER_ENOMEM 12
#define USER_EACCES 13
#define USER_EBUSY 16
#define USER_EEXIST 17
#define USER_EXDEV 18
#define USER_ENOTDIR 20
#define USER_EISDIR 21
#define USER_ENOSPC 28
#define USER_EROFS 30
#define USER_ENAMETOOLONG 36
#define USER_ENOTEMPTY 39
#define USER_EBADF 9
#define USER_EFAULT 14
#define USER_ENODEV 19
#define USER_EINVAL 22
#define USER_ENOSYS 38
#define USER_IO_MAX 4096
#define USER_SLEEP_MAX_MS 9223372036854775807
#define USER_O_READ 1
#define USER_O_WRITE 2
#define USER_O_CREATE 4
#define USER_O_EXCLUSIVE 8
#define USER_O_TRUNCATE 16
#define USER_O_APPEND 32
#define USER_O_DIRECTORY 64
#define USER_SEEK_SET 0
#define USER_SEEK_CUR 1
#define USER_SEEK_END 2
#define USER_UNLINK_DIR 1
#define USER_DT_FILE 1
#define USER_DT_DIR 2
#define USER_PATH_MAX 256
#ifndef __ASSEMBLER__
#include <stdint.h>
struct user_stat { uint64_t size; uint32_t type,reserved; };
struct user_dirent { uint64_t size; uint32_t type,reserved; char name[32]; };
_Static_assert(sizeof(struct user_stat)==16,"stat ABI");
_Static_assert(sizeof(struct user_dirent)==48,"dirent ABI");
#endif
#endif
