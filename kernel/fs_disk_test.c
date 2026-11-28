#include <fat32_internal.h>
#include <programs.h>
#include <process.h>
#include <pmm.h>
#include <vmm.h>
#include <heap.h>
#include <interrupts.h>
#include <runtime.h>
#include <panic.h>
#include <log.h>
static void signature(void) {
    int fd; char buffer[64]; static const char expected[]="NEXUS-FAT32-DISPOSABLE-v1\n";
    KASSERT(vfs_open("/mnt/NEXUS.TST",VFS_READ,&fd)==VFS_OK);
    struct vfs_io r=vfs_read(fd,buffer,sizeof buffer);
    KASSERT(!r.error && r.count==sizeof expected-1 && !memcmp(buffer,expected,r.count) && vfs_close(fd)==VFS_OK);
}
static void memory_equal(const struct pmm_stats *p,const struct heap_stats *h,const struct vfs_stats *v) {
    struct pmm_stats now; struct heap_stats heap; struct vfs_stats vf;
    pmm_get_stats(&now); heap_get_stats(&heap); vfs_get_stats(&vf);
    KASSERT(p->free_pages==now.free_pages && p->allocated_pages==now.allocated_pages);
    KASSERT(h->allocations==heap.allocations && h->requested_bytes==heap.requested_bytes && h->mapped_bytes==heap.mapped_bytes);
    KASSERT(v->descriptors==vf.descriptors && v->sessions==vf.sessions && v->mounts==vf.mounts); heap_check();
}
static void verify_records(uint64_t a,uint64_t b) {
    int fd; KASSERT(vfs_open("/mnt/SHARED.BIN",VFS_READ,&fd)==VFS_OK);
    bool seen[2][16]={{false}}; unsigned int records=0;
    for(;;) {
        uint64_t words[8]; struct vfs_io r=vfs_read(fd,words,sizeof words); KASSERT(!r.error); if(!r.count) break;
        KASSERT(r.count==sizeof words && (words[0]==a || words[0]==b) && words[1]<16);
        unsigned int owner=words[0]==a ? 0:1; KASSERT(!seen[owner][words[1]]); seen[owner][words[1]]=true;
        const unsigned char *bytes=(const unsigned char *)words;
        for(unsigned int i=16;i<64;++i) KASSERT(bytes[i]==(unsigned char)(words[0]+words[1]+i));
        ++records;
    }
    KASSERT(records==32 && vfs_close(fd)==VFS_OK);
}
static uint64_t load(const char *path) {
    enum elf64_result elf; enum vfs_error io;
    uint64_t pid=process_create_elf_file(path,&elf,&io);
    if(!pid) log_error("fs-test","ELF do VFS %s: %s / %s",path,elf64_error(elf),vfs_error_name(io));
    KASSERT(pid); return pid;
}
void fs_disk_persist_selftest(void) {
    const struct block_device *disk=block_get(0); KASSERT(disk); struct fat32 *f; enum fat32_error detail;
    KASSERT(fat32_mount_runtime(disk,"/mnt",0,disk->sectors,false,&f,&detail)==VFS_OK); signature();
    int fd; KASSERT(vfs_open("/mnt",VFS_READ|VFS_OPEN_DIR,&fd)==VFS_OK);
    struct vfs_dirent entry; bool end; unsigned int files=0;
    for(;;) {
        KASSERT(vfs_readdir(fd,&entry,&end)==VFS_OK); if(end) break;
        if(entry.type!=VFS_DIRECTORY || entry.name[0]!='P') continue;
        uint64_t pid=0; size_t i=1;
        for(;entry.name[i]>='0' && entry.name[i]<='9';++i) pid=pid*10+(unsigned int)(entry.name[i]-'0');
        if(!pid || entry.name[i]) continue;
        char path[64]="/mnt/"; strcpy(path+5,entry.name); strcpy(path+strlen(path),"/DATA.BIN");
        int data; KASSERT(vfs_open(path,VFS_READ,&data)==VFS_OK);
        uint8_t buffer[512]; size_t offset=0;
        for(;;) {
            struct vfs_io r=vfs_read(data,buffer,sizeof buffer); KASSERT(!r.error); if(!r.count) break;
            for(size_t j=0;j<r.count;++j) KASSERT(buffer[j]==(uint8_t)(pid*17+(offset+j)*7));
            offset+=r.count;
        }
        KASSERT(offset==12288 && vfs_close(data)==VFS_OK); ++files;
    }
    KASSERT(files>=4 && vfs_close(fd)==VFS_OK);
    struct vfs_stat st; KASSERT(vfs_stat_path("/mnt/COPY.BIN",&st)==VFS_OK && st.size==8197);
    KASSERT(vfs_unmount("/mnt")==VFS_OK);
    log_info("fs-test","FS PERSIST PASS: arquivos de %u processos e copia intactos apos reiniciar.",files);
}
static void crash_point(struct fat32 *f,unsigned int phase) {
    (void)f; KASSERT(phase==1);
    log_warn("fs-test","FS CRASH READY: dados e FAT duraveis; diretorio ainda nao publicado.");
    kernel_halt(); /* Host sends SIGKILL to QEMU, without a guest shutdown. */
}
void fs_disk_crash_selftest(void) {
    const struct block_device *disk=block_get(0); KASSERT(disk); struct fat32 *f; enum fat32_error detail;
    KASSERT(fat32_mount_runtime(disk,"/mnt",0,disk->sectors,true,&f,&detail)==VFS_OK); signature();
    int fd; KASSERT(vfs_open("/mnt/CRASH.BIN",VFS_CREATE|VFS_WRITE,&fd)==VFS_OK);
    static unsigned char data[8197]; for(size_t i=0;i<sizeof data;++i) data[i]=(unsigned char)(i*11+3);
    f->checkpoint=crash_point;
    (void)vfs_write(fd,data,sizeof data); PANIC(ERR_SELFTEST,"Checkpoint de interrupcao nao foi acionado.");
}
void fs_disk_recovery_selftest(bool repaired) {
    const struct block_device *disk=block_get(0); KASSERT(disk); struct fat32 *f; enum fat32_error detail;
    enum vfs_error e=fat32_mount_runtime(disk,"/mnt",0,disk->sectors,true,&f,&detail);
    if(!repaired) {
        KASSERT(e!=VFS_OK && detail==FAT32_DIRTY);
        KASSERT(fat32_mount_runtime(disk,"/mnt",0,disk->sectors,false,&f,&detail)==VFS_OK); signature();
        struct vfs_stat st; KASSERT(vfs_stat_path("/mnt/CRASH.BIN",&st)==VFS_OK && st.size==0);
        KASSERT(vfs_unmount("/mnt")==VFS_OK);
        log_info("fs-test","FS DIRTY PASS: escrita recusada apos corte; leitura e kernel continuam."); return;
    }
    KASSERT(e==VFS_OK); signature();
    int fd; KASSERT(vfs_open("/mnt/SHARED.BIN",VFS_CREATE|VFS_WRITE|VFS_TRUNCATE,&fd)==VFS_OK && vfs_close(fd)==VFS_OK);
    struct pmm_stats p; struct heap_stats h; struct vfs_stats v; pmm_get_stats(&p); heap_get_stats(&h); vfs_get_stats(&v);
    uint64_t pid=load("/mnt/fsio.elf"); int status; KASSERT(process_wait(pid,&status) && !status); memory_equal(&p,&h,&v);
    KASSERT(vfs_touch("/mnt/RECOVER.TXT")==VFS_OK && vfs_unmount("/mnt")==VFS_OK);
    log_info("fs-test","FS RECOVERY PASS: volume reparado, escrita e ELF do disco funcionam, memoria restaurada.");
}
void fs_disk_selftest(void) {
    const struct block_device *disk=block_get(0); KASSERT(disk);
    struct fat32 *f; enum fat32_error detail;
    KASSERT(fat32_mount_runtime(disk,"/mnt",0,disk->sectors,true,&f,&detail)==VFS_OK); signature();
    int fd; KASSERT(vfs_open("/mnt/SHARED.BIN",VFS_CREATE|VFS_WRITE,&fd)==VFS_OK && vfs_close(fd)==VFS_OK);
    struct pmm_stats p; struct heap_stats h; struct vfs_stats v;
    pmm_get_stats(&p); heap_get_stats(&h); vfs_get_stats(&v);
    enum elf64_result elf; enum vfs_error io;
    KASSERT(!process_create_elf_file("/mnt/MISSING.ELF",&elf,&io) && io==VFS_NOT_FOUND); memory_equal(&p,&h,&v);
    KASSERT(!process_create_elf_file("/mnt",&elf,&io) && io==VFS_IS_DIR); memory_equal(&p,&h,&v);
    KASSERT(!process_create_elf_file("/mnt/BAD.ELF",&elf,&io) && elf==ELF64_TRUNCATED); memory_equal(&p,&h,&v);
    KASSERT(!process_create_elf_file("/mnt/RWX.ELF",&elf,&io) && elf==ELF64_PERMISSIONS); memory_equal(&p,&h,&v);
    KASSERT(!process_create_elf_file("/mnt/HUGE.ELF",&elf,&io) && elf==ELF64_LIMIT); memory_equal(&p,&h,&v);
    for(unsigned int cycle=0;cycle<2;++cycle) {
        KASSERT(vfs_truncate("/mnt/SHARED.BIN",0)==VFS_OK);
        int reserved,other; KASSERT(vfs_open("/etc/motd",VFS_READ,&reserved)==VFS_OK && vfs_open("/etc/motd",VFS_READ,&other)==VFS_OK && other==4);
        pmm_get_stats(&p); heap_get_stats(&h); vfs_get_stats(&v);
        process_preempt_disable(); uint64_t a=load("/mnt/fsio.elf"),b=load("/mnt/fsio.elf"); process_preempt_enable();
        int status; KASSERT(process_wait(a,&status)); log_info("fs-test","Disk ELF PID=%llu status=%u",(unsigned long long)a,(unsigned int)status); KASSERT(!status);
        KASSERT(process_wait(b,&status)); log_info("fs-test","Disk ELF PID=%llu status=%u",(unsigned long long)b,(unsigned int)status); KASSERT(!status);
        memory_equal(&p,&h,&v); verify_records(a,b);
        KASSERT(vfs_close(reserved)==VFS_OK && vfs_close(other)==VFS_OK);
        log_info("fs-test","Disk ciclo=%u: Ring3 concorrente, arquivos > cluster, append e exit sem vazamentos.",cycle+1);
    }
    pmm_get_stats(&p); heap_get_stats(&h); vfs_get_stats(&v);
    uint64_t victim=load("/mnt/HOLD.ELF"); bool blocked=false; uint64_t start=timer_ticks();
    while(!blocked && timer_ticks()-start<300) {
        struct process_info processes[PROCESS_MAX]; size_t count=process_snapshot(processes,PROCESS_MAX);
        struct vfs_stats vf; vfs_get_stats(&vf);
        for(size_t i=0;i<count;++i) if(processes[i].pid==victim && processes[i].state==PROCESS_BLOCKED &&
            processes[i].blocked_on==PROCESS_BLOCK_SLEEP && vf.descriptors>v.descriptors) blocked=true;
        if(!blocked) process_sleep(1);
    }
    KASSERT(blocked && vfs_unmount("/mnt")==VFS_BUSY && process_kill(victim,-9));
    int status; KASSERT(process_wait(victim,&status) && status==-9); memory_equal(&p,&h,&v);
    uint64_t bad=load("/mnt/FAULT.ELF"); KASSERT(process_wait(bad,&status) && status==-262); memory_equal(&p,&h,&v);
    KASSERT(vfs_copy_file("/mnt/FRAG.BIN","/mnt/COPY.BIN")==VFS_OK);
    KASSERT(vfs_copy_file("/mnt/COPY.BIN","/mnt/./copy.bin")==VFS_INVALID);
    KASSERT(vfs_touch("/mnt/TOUCH.TXT")==VFS_OK && vfs_touch("/mnt/COPY.BIN")==VFS_OK);
    struct vfs_stat st; KASSERT(vfs_stat_path("/mnt/COPY.BIN",&st)==VFS_OK && st.size==8197);
    struct vfs_space space; KASSERT(vfs_statvfs("/mnt",&space)==VFS_OK && space.free_known && !space.approximate && space.free_bytes<space.total_bytes);
    KASSERT(vfs_sync_all()==VFS_OK && vfs_unmount("/mnt")==VFS_OK);
    log_info("fs-test","FS DISK PASS: ELF do disco, syscalls, kill/fault, FDs/memoria, cp/touch/df e sync.");
}
