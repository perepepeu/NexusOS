#include <vfs_internal.h>
#include <ramfs.h>
#include <process.h>
#include <io.h>
#include <log.h>

static struct vfs state;
static struct ramfs rootfs,tmpfs;
static bool locked;
static bool enter(void) {
    uint64_t flags; __asm__ volatile("pushfq; pop %0":"=r"(flags));
    if(!(flags&(1u<<9)) || !state.ready) return false;
    process_preempt_disable();
    if(locked) { process_preempt_enable(); return false; }
    locked=true; return true;
}
static void leave(void) { locked=false; process_preempt_enable(); }
/* The outer lock spans path lookup, backend I/O, offset/refcount changes and
 * append's EOF selection. Backend storage may nest preempt_disable safely. */
#define CALL(expression) do { if(!enter()) return VFS_CONTEXT; enum vfs_error e=(expression); leave(); return e; } while(0)
enum vfs_error vfs_open(const char *p,unsigned int f,int *fd) { CALL(vfs_core_open(&state,process_current_pid(),p,f,fd)); }
enum vfs_error vfs_close(int fd) { CALL(vfs_core_close(&state,process_current_pid(),fd)); }
struct vfs_io vfs_read(int fd,void *b,size_t n) {
    if(!enter()) return (struct vfs_io){VFS_CONTEXT,0};
    struct vfs_io r=vfs_core_read(&state,process_current_pid(),fd,b,n); leave(); return r;
}
struct vfs_io vfs_write(int fd,const void *b,size_t n) {
    if(!enter()) return (struct vfs_io){VFS_CONTEXT,0};
    struct vfs_io r=vfs_core_write(&state,process_current_pid(),fd,b,n); leave(); return r;
}
enum vfs_error vfs_seek(int fd,int64_t o,enum vfs_whence w,uint64_t *out) { CALL(vfs_core_seek(&state,process_current_pid(),fd,o,w,out)); }
enum vfs_error vfs_readdir(int fd,struct vfs_dirent *out,bool *end) { CALL(vfs_core_readdir(&state,process_current_pid(),fd,out,end)); }
enum vfs_error vfs_stat_path(const char *p,struct vfs_stat *out) { CALL(vfs_core_stat(&state,process_current_pid(),p,out)); }
enum vfs_error vfs_mkdir(const char *p) { CALL(vfs_core_mkdir(&state,process_current_pid(),p)); }
enum vfs_error vfs_chdir(const char *p) { CALL(vfs_core_chdir(&state,process_current_pid(),p)); }
enum vfs_error vfs_getcwd(char *p,size_t n) { CALL(vfs_core_getcwd(&state,process_current_pid(),p,n)); }
enum vfs_error vfs_mount(const char *p,struct vfs_volume *v) { CALL(vfs_core_mount(&state,process_current_pid(),p,v)); }
enum vfs_error vfs_unmount(const char *p) { CALL(vfs_core_unmount(&state,process_current_pid(),p)); }
enum vfs_error vfs_truncate(const char *p,uint64_t n) { CALL(vfs_core_truncate(&state,process_current_pid(),p,n)); }
enum vfs_error vfs_rename(const char *a,const char *b) { CALL(vfs_core_rename(&state,process_current_pid(),a,b)); }
enum vfs_error vfs_remove(const char *p,bool dir) { CALL(vfs_core_remove(&state,process_current_pid(),p,dir)); }
enum vfs_error vfs_sync(const char *p) { CALL(vfs_core_sync(&state,process_current_pid(),p)); }
enum vfs_error vfs_sync_all(void) { CALL(vfs_core_sync_all(&state)); }
enum vfs_error vfs_volume_mounted(const struct vfs_volume *volume,bool *mounted) {
    if(!volume || !mounted) return VFS_INVALID;
    if(!enter()) return VFS_CONTEXT;
    *mounted=false;
    for(unsigned int i=0;i<VFS_MOUNTS;++i) if(state.mounts[i].used && state.mounts[i].volume==volume) *mounted=true;
    leave(); return VFS_OK;
}
void vfs_get_stats(struct vfs_stats *out) {
    if(!out) return;
    *out=(struct vfs_stats){0};
    if(enter()) { vfs_core_stats(&state,out); leave(); }
}
void vfs_process_reap(uint64_t pid) { vfs_core_reap(&state,pid); }
bool vfs_init(void) {
    if(state.ready) return false;
    ramfs_init(&rootfs); ramfs_init(&tmpfs);
    if(vfs_core_init(&state,&rootfs.volume)!=VFS_OK) return false;
    if(vfs_mkdir("/mnt") || vfs_mkdir("/tmp") || vfs_mkdir("/etc") || vfs_mount("/tmp",&tmpfs.volume)) return false;
    int fd;
    if(vfs_open("/etc/motd",VFS_CREATE|VFS_WRITE,&fd)) return false;
    static const char message[]="NexusOS: VFS e RAMFS prontos.\n";
    struct vfs_io r=vfs_write(fd,message,sizeof message-1);
    enum vfs_error closed=vfs_close(fd);
    if(r.error || r.count!=sizeof message-1 || closed) return false;
    log_info("vfs","VFS pronto: raiz RAMFS, /tmp montado; 29 descritores por PID, threads compartilham FDs.");
    return true;
}
