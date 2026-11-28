#include <vfs.h>
#include <ramfs.h>
#include <process.h>
#include <interrupts.h>
#include <pmm.h>
#include <heap.h>
#include <io.h>
#include <runtime.h>
#include <panic.h>
#include <log.h>

static struct ramfs volume;
static struct vfs_ops slow_ops;
static const struct vfs_ops *original;
static unsigned int delays;
static volatile bool victim_ready;
static struct worker { unsigned int index; int fd; } workers[2];
static struct writer { struct worker *worker; unsigned int tag; } writers[4];
static void spin(uint64_t ticks) {
    uint64_t start=timer_ticks();
    while(timer_ticks()-start<ticks) __asm__ volatile("pause" : : : "memory");
}
static struct vfs_io slow_write(struct vfs_node *node,uint64_t offset,const void *b,size_t bytes) {
    if(delays) {
        --delays; uint64_t pid=process_current_pid(),tid=thread_current_tid();
        spin(8); KASSERT(pid==process_current_pid() && tid==thread_current_tid());
    }
    return original->write(node,offset,b,bytes);
}
static void writer_thread(void *argument) {
    struct writer *w=argument; char record[8]; memset(record,'A'+w->tag,sizeof record);
    for(unsigned int i=0;i<64;++i) {
        struct vfs_io r=vfs_write(w->worker->fd,record,sizeof record);
        KASSERT(r.error==VFS_OK && r.count==sizeof record); spin(1);
    }
}
static void process_worker(void *argument) {
    struct worker *w=argument; char cwd[VFS_PATH_MAX];
    KASSERT(vfs_getcwd(cwd,sizeof cwd)==VFS_OK && !strcmp(cwd,"/"));
    KASSERT(vfs_read(4,cwd,1).error==VFS_BAD_FD);
    KASSERT(vfs_chdir("/mnt")==VFS_OK);
    KASSERT(vfs_open(w->index ? "p1":"p0",VFS_CREATE|VFS_READ|VFS_WRITE,&w->fd)==VFS_OK && w->fd==3);
    uint64_t t[2];
    for(unsigned int i=0;i<2;++i) {
        struct writer *writer=&writers[w->index*2+i]; *writer=(struct writer){w,w->index*2+i};
        t[i]=thread_create("vfs-writer",writer_thread,writer); KASSERT(t[i]);
    }
    int status; KASSERT(thread_join(t[0],&status) && status==0);
    KASSERT(thread_join(t[1],&status) && status==0);
    uint64_t position; KASSERT(vfs_seek(w->fd,0,VFS_SEEK_CUR,&position)==VFS_OK && position==1024);
    KASSERT(vfs_seek(w->fd,0,VFS_SEEK_SET,&position)==VFS_OK);
    unsigned int counts[2]={0,0};
    for(unsigned int i=0;i<128;++i) {
        char record[8]; struct vfs_io r=vfs_read(w->fd,record,sizeof record);
        KASSERT(!r.error && r.count==sizeof record);
        unsigned int tag=(unsigned int)(record[0]-'A')-w->index*2; KASSERT(tag<2); ++counts[tag];
        for(unsigned int j=1;j<sizeof record;++j) KASSERT(record[j]==record[0]);
    }
    KASSERT(counts[0]==64 && counts[1]==64);
    /* Intentionally leave fd and cwd pinned: process teardown must release both. */
}
static void append_worker(void *argument) {
    unsigned int index=(unsigned int)(uintptr_t)argument; int fd;
    KASSERT(vfs_open("/mnt/append",VFS_CREATE|VFS_WRITE|VFS_APPEND,&fd)==VFS_OK);
    char record[4]; memset(record,'x'+index,sizeof record);
    for(unsigned int i=0;i<32;++i) {
        struct vfs_io r=vfs_write(fd,record,sizeof record); KASSERT(!r.error && r.count==sizeof record); spin(1);
    }
}
static void victim(void *argument) {
    (void)argument; int fd;
    KASSERT(vfs_chdir("/mnt")==VFS_OK && vfs_open("p0",VFS_READ,&fd)==VFS_OK);
    victim_ready=true; for(;;) process_sleep(100);
}
void vfs_selftest(void) {
    struct pmm_stats before,after; struct heap_stats hb,ha; struct vfs_stats base,end;
    pmm_get_stats(&before); heap_get_stats(&hb); vfs_get_stats(&base);
    ramfs_init(&volume); KASSERT(vfs_mount("/mnt",&volume.volume)==VFS_OK);
    KASSERT(vfs_mkdir("/mnt/dir")==VFS_OK && vfs_chdir("/mnt/dir")==VFS_OK);
    char cwd[VFS_PATH_MAX]; KASSERT(vfs_getcwd(cwd,sizeof cwd)==VFS_OK && !strcmp(cwd,"/mnt/dir"));
    int fd,other; KASSERT(vfs_open("./data",VFS_CREATE|VFS_READ|VFS_WRITE,&fd)==VFS_OK);
    KASSERT(vfs_write(fd,"abcdef",6).count==6); uint64_t position;
    KASSERT(vfs_seek(fd,-3,VFS_SEEK_END,&position)==VFS_OK && position==3);
    char data[8]={0}; KASSERT(vfs_read(fd,data,sizeof data).count==3 && !memcmp(data,"def",3));
    KASSERT(vfs_unmount("/mnt")==VFS_BUSY); KASSERT(vfs_close(fd)==VFS_OK);
    KASSERT(vfs_unmount("/mnt")==VFS_BUSY); KASSERT(vfs_chdir("../../..")==VFS_OK);
    KASSERT(vfs_getcwd(cwd,sizeof cwd)==VFS_OK && !strcmp(cwd,"/"));
    uint64_t flags=irq_save(); enum vfs_error context=vfs_open("/etc/motd",VFS_READ,&fd); irq_restore(flags);
    KASSERT(context==VFS_CONTEXT);
    log_info("vfs-test","Caminhos absolutos/relativos, ./.., offset e unmount ocupado: OK.");
    KASSERT(vfs_open("/etc/motd",VFS_READ,&fd)==VFS_OK);
    KASSERT(vfs_open("/etc/motd",VFS_READ,&other)==VFS_OK && other==4);
    original=volume.volume.ops; slow_ops=*original; slow_ops.write=slow_write; volume.volume.ops=&slow_ops; delays=4;
    struct scheduler_info sched_before,sched_after; scheduler_snapshot(&sched_before);
    uint64_t pids[2];
    for(unsigned int i=0;i<2;++i) {
        workers[i]=(struct worker){.index=i}; pids[i]=process_create("vfs-fds",process_worker,&workers[i]); KASSERT(pids[i]);
    }
    int status;
    for(unsigned int i=0;i<2;++i) KASSERT(process_wait(pids[i],&status) && status==0);
    volume.volume.ops=original; scheduler_snapshot(&sched_after);
    KASSERT(sched_after.preemptions>sched_before.preemptions && sched_after.deferred_ticks>sched_before.deferred_ticks && !delays);
    KASSERT(vfs_close(fd)==VFS_OK && vfs_close(other)==VFS_OK);
    log_info("vfs-test","FDs isolados por PID; threads compartilham offsets; PIT durante callback; preempcao adiada: OK.");
    for(unsigned int i=0;i<2;++i) { pids[i]=process_create("vfs-append",append_worker,(void *)(uintptr_t)i); KASSERT(pids[i]); }
    for(unsigned int i=0;i<2;++i) KASSERT(process_wait(pids[i],&status) && status==0);
    KASSERT(vfs_open("/mnt/append",VFS_READ,&fd)==VFS_OK);
    unsigned int counts[2]={0,0};
    for(unsigned int i=0;i<64;++i) {
        char record[4]; struct vfs_io r=vfs_read(fd,record,sizeof record); KASSERT(!r.error && r.count==4);
        unsigned int tag=(unsigned int)(record[0]-'x'); KASSERT(tag<2); ++counts[tag];
        KASSERT(record[0]==record[1] && record[1]==record[2] && record[2]==record[3]);
    }
    KASSERT(counts[0]==32 && counts[1]==32 && vfs_read(fd,data,1).count==0); KASSERT(vfs_close(fd)==VFS_OK);
    for(unsigned int i=0;i<4;++i) {
        victim_ready=false; uint64_t pid=process_create("vfs-kill",victim,0); KASSERT(pid);
        uint64_t start=timer_ticks();
        while(!victim_ready && timer_ticks()-start<100) process_sleep(1);
        KASSERT(victim_ready && vfs_unmount("/mnt")==VFS_BUSY);
        KASSERT(process_kill(pid,-9) && process_wait(pid,&status) && status==-9);
    }
    KASSERT(vfs_unmount("/mnt")==VFS_OK);
    KASSERT(vfs_mount("/mnt",&volume.volume)==VFS_OK);
    struct vfs_stat st; KASSERT(vfs_stat_path("/mnt/append",&st)==VFS_OK && st.size==256);
    KASSERT(vfs_unmount("/mnt")==VFS_OK);
    pmm_get_stats(&after); heap_get_stats(&ha); vfs_get_stats(&end);
    KASSERT(before.free_pages==after.free_pages && before.allocated_pages==after.allocated_pages);
    KASSERT(hb.allocations==ha.allocations && hb.requested_bytes==ha.requested_bytes);
    KASSERT(base.mounts==end.mounts && base.sessions==end.sessions && base.descriptors==end.descriptors);
    log_info("vfs-test","Append concorrente, exit/kill e remount: OK; sem FDs, referencias ou paginas vazadas.");
    log_info("vfs-test","VFS SELFTEST PASS");
}
