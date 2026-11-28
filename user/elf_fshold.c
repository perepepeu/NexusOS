#include <syscall.h>
int elf_main(const uint64_t *stack) {
    (void)stack;
    if(sys_open("/mnt/SHARED.BIN",USER_O_READ|USER_O_WRITE)<3 || sys_chdir("/mnt")) return 101;
    static const char ready[]="FSHOLD READY\n"; (void)sys_write(1,ready,sizeof ready-1);
    for(;;) (void)sys_sleep(1000);
}
