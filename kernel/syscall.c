#include <syscall.h>
#include <process.h>
#include <console.h>
#include <serial.h>
#include <io.h>

int64_t sys_write(struct address_space *space, uint64_t fd, uint64_t buffer, uint64_t bytes) {
    if (fd != 1 && fd != 2) return -USER_EBADF;
    if (bytes > USER_IO_MAX) return -USER_EINVAL;
    if (!bytes) return 0;
    if (!address_user_check(space, buffer, bytes, false)) return -USER_EFAULT;
    char bounce[256];
    for (uint64_t done = 0; done < bytes;) {
        size_t count = bytes - done;
        if (count > sizeof bounce) count = sizeof bounce;
        if (!address_user_copy(space, buffer + done, bounce, count, false)) return done ? (int64_t)done : -USER_EFAULT;
        uint64_t flags = irq_save();
        console_write_n(bounce, count); serial_write_n(bounce, count);
        irq_restore(flags); done += count;
    }
    return (int64_t)bytes;
}
int64_t sys_sleep(uint64_t milliseconds) {
    if (milliseconds > USER_SLEEP_MAX_MS) return -USER_EINVAL;
    _Static_assert(SCHEDULER_PIT_HZ == 100, "Conversao de sleep exige PIT de 100 Hz");
    /* Um tick extra cobre a fracao ja transcorrida no instante da chamada. */
    process_sleep(milliseconds ? milliseconds / 10 + (milliseconds % 10 != 0) + 1 : 0);
    return 0;
}
int64_t sys_yield(void) { process_yield(); return 0; }
int64_t sys_getpid(void) { return (int64_t)process_current_pid(); }
__attribute__((noreturn)) void sys_exit(uint64_t status) { process_exit((int32_t)(uint32_t)status); }
int64_t syscall_dispatch(struct address_space *space, const struct interrupt_frame *frame) {
    switch (frame->rax) {
        case USER_SYSCALL_EXIT: sys_exit(frame->rdi);
        case USER_SYSCALL_YIELD: return sys_yield();
        case USER_SYSCALL_GETPID: return sys_getpid();
        case USER_SYSCALL_GETTID: return (int64_t)thread_current_tid();
        case USER_SYSCALL_TICKS: return (int64_t)timer_ticks();
        case USER_SYSCALL_WRITE: return sys_write(space, frame->rdi, frame->rsi, frame->rdx);
        case USER_SYSCALL_SLEEP: return sys_sleep(frame->rdi);
        default: return -USER_ENOSYS;
    }
}
