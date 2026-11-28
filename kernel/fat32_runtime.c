#include <fat32.h>
#include <process.h>
#include <runtime.h>
#include <log.h>
#include <panic.h>
#include <io.h>
#include <fat32_internal.h>
static struct fat32 volumes[2];
enum vfs_error fat32_mount_runtime(const struct block_device *d,const char *path,uint64_t start,uint64_t length,
    bool writable,struct fat32 **selected,enum fat32_error *detail) {
    *selected=0; *detail=FAT32_OK;
    uint64_t flags; __asm__ volatile("pushfq; pop %0":"=r"(flags));
    if(!(flags&(1u<<9))) return VFS_CONTEXT;
    if(!d || !length || start>=d->sectors || length>d->sectors-start) { *detail=FAT32_ARGUMENT; return VFS_INVALID; }
    process_preempt_disable();
    enum vfs_error result=VFS_BUSY;
    for(unsigned int i=0;i<2;++i) {
        bool mounted; result=vfs_volume_mounted(&volumes[i].volume,&mounted); if(result) { process_preempt_enable(); return result; }
        struct fat32 *f=&volumes[i];
        if(mounted && f->volume.device==d && start<f->geometry.start+f->geometry.length && f->geometry.start<start+length && (writable || f->writable)) {
            process_preempt_enable(); return VFS_BUSY;
        }
    }
    for(unsigned int i=0;i<2;++i) {
        bool mounted; result=vfs_volume_mounted(&volumes[i].volume,&mounted); if(result) break;
        if(mounted) { result=VFS_BUSY; continue; }
        struct fat32 *f=&volumes[i]; *selected=f; *detail=fat32_open(f,d,start,length);
        if(!*detail && writable) *detail=fat32_enable_write(f,fat32_rtc);
        if(*detail) { result=VFS_IO; break; }
        result=vfs_mount(path,&f->volume); break;
    }
    process_preempt_enable(); return result;
}
static void info(struct fat32 *f) {
    process_preempt_disable();
    struct fat32_geometry *g=&f->geometry;
    log_info("fat32","%s: LBA=%llu setores=%llu bytes/setor=%u cluster=%u clusters=%u raiz=%u",
        f->volume.device->name,(unsigned long long)g->start,(unsigned long long)g->total,g->sector_size,g->cluster_size,g->clusters,g->root);
    log_info("fat32","Reservados=%u; FATs=%u x %u setores; ativa=%u; FAT=%llu dados=%llu",
        g->reserved,g->fat_count,g->fat_size,g->active_fat,(unsigned long long)g->fat_start,(unsigned long long)g->data_start);
    log_info("fat32","FSInfo=%s; livres=%s (%u); proximo=%s (%u); ultimo erro=%s",
        g->fsinfo_valid ? "valido":g->fsinfo_present ? "invalido/ignorado":"ausente",
        g->free_known ? "conhecido":"desconhecido",g->free_clusters,
        g->next_known ? "conhecido":"desconhecido",g->next_free,fat32_error_name(f->error));
    process_preempt_enable();
}
static bool number(const char *text,uint64_t *out) {
    if(!*text) return false;
    uint64_t n=0;
    for(;*text;++text) {
        if(*text<'0' || *text>'9' || n>(UINT64_MAX-(unsigned int)(*text-'0'))/10) return false;
        n=n*10+(unsigned int)(*text-'0');
    }
    *out=n; return true;
}
bool fat32_terminal_command(const char *name,const char *args) {
    if(!strcmp(name,"fatinfo")) {
        if(*args) { log_info("fat32","Uso: fatinfo"); return true; }
        for(unsigned int i=0;i<2;++i) {
            bool mounted; if(!vfs_volume_mounted(&volumes[i].volume,&mounted) && mounted) info(&volumes[i]);
        }
        return true;
    }
    bool writable=!strcmp(name,"mountfatrw");
    if(strcmp(name,"mountfat") && !writable) return false;
    char copy[VFS_PATH_MAX]; size_t size=strlen(args);
    if(size>=sizeof copy) { log_error("fat32","Argumentos longos."); return true; }
    memcpy(copy,args,size+1); char *tokens[5]; unsigned int count=0; char *p=copy;
    while(*p && count<5) {
        while(*p==' ' || *p=='\t') ++p;
        if(!*p) break;
        tokens[count++]=p; while(*p && *p!=' ' && *p!='\t') ++p;
        if(*p) *p++=0;
    }
    if(count!=2 && count!=4) { log_info("fat32","Uso: mountfat|mountfatrw DISCO DIRETORIO [LBA SETORES]"); return true; }
    const struct block_device *disk=0;
    for(size_t i=0;i<block_count();++i) if(!strcmp(block_get(i)->name,tokens[0])) disk=block_get(i);
    if(!disk) { log_error("fat32","Disco nao encontrado. Digite disks."); return true; }
    uint64_t start=0,length=disk->sectors;
    if(count==4 && (!number(tokens[2],&start) || !number(tokens[3],&length))) {
        log_error("fat32","LBA e SETORES devem ser inteiros decimais."); return true;
    }
    struct fat32 *f; enum fat32_error detail;
    enum vfs_error e=fat32_mount_runtime(disk,tokens[1],start,length,writable,&f,&detail);
    if(e) log_error("fat32","Montagem recusada: %s.",detail ? fat32_error_name(detail):vfs_error_name(e));
    else { log_info("fat32",writable ? "FAT32 montado para leitura e escrita em %s.":"FAT32 montado somente para leitura em %s.",tokens[1]); info(f); }
    return true;
}
void fat32_selftest(bool invalid,uint64_t start,uint64_t length) {
    const struct block_device *d=block_get(0); KASSERT(d);
    if(!length) length=d->sectors-start;
    struct fat32 *f; enum fat32_error detail;
    enum vfs_error e=fat32_mount_runtime(d,"/mnt",start,length,false,&f,&detail);
    if(invalid) {
        KASSERT(e!=VFS_OK && (detail==FAT32_GEOMETRY || detail==FAT32_TYPE || detail==FAT32_CYCLE));
        log_info("fat32-test","FAT32 REJECT PASS: %s",fat32_error_name(detail)); return;
    }
    if(e) log_error("fat32-test","Montagem: %s; VFS=%s; bloco=%s status=%x erro=%x",fat32_error_name(detail),
        vfs_error_name(e),f ? block_error_name(f->block_error.error):"sem volume",f ? f->block_error.status_reg:0,f ? f->block_error.error_reg:0);
    KASSERT(!e && f); info(f);
    int fd; KASSERT(vfs_open("/mnt/frag.bin",VFS_READ,&fd)==VFS_OK);
    uint8_t buffer[333]; size_t offset=0; uint32_t hash=2166136261u;
    for(;;) {
        struct vfs_io r=vfs_read(fd,buffer,sizeof buffer); KASSERT(!r.error); if(!r.count) break;
        for(size_t i=0;i<r.count;++i) {
            KASSERT(buffer[i]==(uint8_t)((offset+i)*7+11)); hash=(hash^buffer[i])*16777619u;
        }
        offset+=r.count;
    }
    KASSERT(offset==8197 && vfs_unmount("/mnt")==VFS_BUSY);
    uint64_t position; KASSERT(vfs_seek(fd,-5,VFS_SEEK_END,&position)==VFS_OK && position==8192);
    KASSERT(vfs_read(fd,buffer,sizeof buffer).count==5 && buffer[0]==(uint8_t)(8192*7+11));
    KASSERT(vfs_close(fd)==VFS_OK);
    KASSERT(vfs_open("/mnt/SUB/child.txt",VFS_READ,&fd)==VFS_OK);
    struct vfs_io r=vfs_read(fd,buffer,sizeof buffer); KASSERT(!r.error && r.count==11 && !memcmp(buffer,"fat32-child",11));
    KASSERT(vfs_close(fd)==VFS_OK);
    KASSERT(vfs_open("/mnt/EMPTY",VFS_READ,&fd)==VFS_OK && !vfs_read(fd,buffer,1).count && vfs_close(fd)==VFS_OK);
    KASSERT(vfs_open("/mnt/FRAG.BIN",VFS_WRITE,&fd)==VFS_READ_ONLY && vfs_mkdir("/mnt/NEW")==VFS_READ_ONLY);
    struct vfs_stat st; KASSERT(vfs_stat_path("/mnt/BROKEN.BIN",&st)==VFS_IO && f->error==FAT32_CYCLE);
    KASSERT(vfs_stat_path("/mnt/TAIL.TXT",&st)==VFS_OK && st.size==4);
    KASSERT(vfs_open("/mnt",VFS_READ|VFS_OPEN_DIR,&fd)==VFS_OK);
    struct vfs_dirent entry; bool end=false,alias=false;
    for(;;) {
        KASSERT(vfs_readdir(fd,&entry,&end)==VFS_OK); if(end) break;
        for(size_t i=0;entry.name[i];++i) if(entry.name[i]=='~') alias=true;
    }
    KASSERT(alias && vfs_close(fd)==VFS_OK);
    KASSERT(vfs_chdir("/mnt/SUB")==VFS_OK && vfs_unmount("/mnt")==VFS_BUSY && vfs_chdir("../..")==VFS_OK);
    KASSERT(vfs_unmount("/mnt")==VFS_OK);
    e=fat32_mount_runtime(d,"/mnt",start,length,false,&f,&detail); KASSERT(!e);
    KASSERT(vfs_stat_path("/mnt/FRAG.BIN",&st)==VFS_OK && st.size==8197 && vfs_unmount("/mnt")==VFS_OK);
    log_info("fat32-test","FAT32 SELFTEST PASS bytes=%u hash=%x; fragmentacao, 8.3/LFN, ciclo e remount OK.",(unsigned int)offset,hash);
}
