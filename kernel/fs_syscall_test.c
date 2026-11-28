#include <programs.h>
#include <process.h>
#include <vfs.h>
#include <pmm.h>
#include <heap.h>
#include <panic.h>
#include <log.h>
void fs_syscall_selftest(void) {
    const struct boot_program *program=programs_find("fsio"); KASSERT(program && program->validation==ELF64_OK);
    int fd,other; KASSERT(vfs_open("/tmp/SHARED.BIN",VFS_CREATE|VFS_WRITE|VFS_TRUNCATE,&fd)==VFS_OK && vfs_close(fd)==VFS_OK);
    KASSERT(vfs_open("/etc/motd",VFS_READ,&fd)==VFS_OK && vfs_open("/etc/motd",VFS_READ,&other)==VFS_OK && other==4);
    struct pmm_stats before,after; struct heap_stats hb,ha; struct vfs_stats vb,va;
    pmm_get_stats(&before); heap_get_stats(&hb); vfs_get_stats(&vb);
    process_preempt_disable();
    uint64_t a=process_create_elf("ramfsio",program->image,program->bytes,0),b=process_create_elf("ramfsio",program->image,program->bytes,0);
    KASSERT(a && b); process_preempt_enable();
    int status; KASSERT(process_wait(a,&status)); log_info("fs-test","RAMFS Ring3 PID=%llu status=%u",(unsigned long long)a,(unsigned int)status); KASSERT(!status);
    KASSERT(process_wait(b,&status)); log_info("fs-test","RAMFS Ring3 PID=%llu status=%u",(unsigned long long)b,(unsigned int)status); KASSERT(!status);
    pmm_get_stats(&after); heap_get_stats(&ha); vfs_get_stats(&va);
    KASSERT(before.free_pages==after.free_pages && hb.allocations==ha.allocations && hb.requested_bytes==ha.requested_bytes);
    KASSERT(vb.descriptors==va.descriptors && vb.sessions==va.sessions && vb.mounts==va.mounts);
    KASSERT(vfs_close(fd)==VFS_OK && vfs_close(other)==VFS_OK);
    log_info("fs-test","FS SYSCALL PASS: Ring3, ponteiros, permissoes, offsets, FDs por PID, concorrencia e exit sem vazamentos.");
}
