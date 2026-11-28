#ifndef NEXUS_KERNEL_SYSCALL_H
#define NEXUS_KERNEL_SYSCALL_H
#include <address_space.h>
#include <interrupts.h>
#include <syscall_abi.h>
/* Somente o dispatcher validado de CPL3, em contexto de tarefa com IF=1.
 * Nenhum destes ponteiros de usuario e dereferenciado diretamente. */
int64_t sys_write(struct address_space *, uint64_t fd, uint64_t buffer, uint64_t bytes);
int64_t sys_read(struct address_space *, uint64_t fd, uint64_t buffer, uint64_t bytes);
int64_t sys_open(struct address_space *,uint64_t path,uint64_t flags);
int64_t sys_close(uint64_t fd);
int64_t sys_seek(uint64_t fd,int64_t offset,uint64_t whence);
int64_t sys_stat(struct address_space *,uint64_t path,uint64_t out);
int64_t sys_mkdir(struct address_space *,uint64_t path);
int64_t sys_unlink(struct address_space *,uint64_t path,uint64_t flags);
int64_t sys_readdir(struct address_space *,uint64_t fd,uint64_t out);
int64_t sys_chdir(struct address_space *,uint64_t path);
int64_t sys_getcwd(struct address_space *,uint64_t out,uint64_t bytes);
int64_t sys_sync(struct address_space *,uint64_t path);
int64_t syscall_file_io(struct address_space *,uint64_t,uint64_t,uint64_t,bool);
int64_t sys_sleep(uint64_t milliseconds);
int64_t sys_yield(void);
int64_t sys_getpid(void);
__attribute__((noreturn)) void sys_exit(uint64_t status);
int64_t syscall_dispatch(struct address_space *, const struct interrupt_frame *);
#endif
