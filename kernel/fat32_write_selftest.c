#include <fat32_internal.h>
#include <runtime.h>
#include <panic.h>
#include <log.h>
static uint8_t pattern[8197];
static void write_file(const char *path,const void *data,size_t bytes,unsigned int flags) {
    int fd; KASSERT(vfs_open(path,flags,&fd)==VFS_OK);
    struct vfs_io r=vfs_write(fd,data,bytes); KASSERT(!r.error && r.count==bytes); KASSERT(vfs_close(fd)==VFS_OK);
}
static void read_text(const char *path,const char *expected) {
    char buffer[128]; int fd; KASSERT(vfs_open(path,VFS_READ,&fd)==VFS_OK);
    struct vfs_io r=vfs_read(fd,buffer,sizeof buffer); KASSERT(!r.error && r.count==strlen(expected) && !memcmp(buffer,expected,r.count));
    KASSERT(!vfs_read(fd,buffer,1).count && vfs_close(fd)==VFS_OK);
}
static void verify_result(void) {
    int fd; KASSERT(vfs_open("/mnt/B/RESULT.BIN",VFS_READ,&fd)==VFS_OK);
    uint8_t buffer[333]; size_t offset=0;
    for(;;) {
        struct vfs_io r=vfs_read(fd,buffer,sizeof buffer); KASSERT(!r.error); if(!r.count) break;
        for(size_t i=0;i<r.count;++i) {
            size_t at=offset+i; uint8_t expected=at<700 ? (uint8_t)(at*7+11):at<1300 ? 0:at<1304 ? (uint8_t)"TAIL"[at-1300]:255;
            KASSERT(at<1304 && buffer[i]==expected);
        }
        offset+=r.count;
    }
    KASSERT(offset==1304 && vfs_close(fd)==VFS_OK);
    read_text("/mnt/B/SUB/NOTE.TXT","moved-dir\n");
    read_text("/mnt/RENAMED.TXT","long-name-alias\nmore\n");
    struct vfs_stat st;
    KASSERT(vfs_stat_path("/mnt/A",&st)==VFS_NOT_FOUND && vfs_stat_path("/mnt/DELETE.TXT",&st)==VFS_NOT_FOUND);
    KASSERT(vfs_stat_path("/mnt/GROW",&st)==VFS_NOT_FOUND);
}
void fat32_write_selftest(unsigned int mode) {
    const struct block_device *disk=block_get(0); KASSERT(disk);
    struct fat32 *f; enum fat32_error detail;
    enum vfs_error e=fat32_mount_runtime(disk,"/mnt",0,disk->sectors,mode!=1,&f,&detail);
    if(e) log_error("fat32-test","RW mount: %s / %s",fat32_error_name(detail),vfs_error_name(e));
    KASSERT(!e && f);
    read_text("/mnt/NEXUS.TST","NEXUS-FAT32-DISPOSABLE-v1\n");
    if(mode==1) {
        verify_result(); KASSERT(vfs_unmount("/mnt")==VFS_OK);
        log_info("fat32-test","FAT32 PERSIST PASS: conteudo, tamanho, move e exclusoes persistiram."); return;
    }
    if(mode==3) {
        int fd; KASSERT(vfs_open("/mnt/FAULT.BIN",VFS_CREATE|VFS_WRITE,&fd)==VFS_OK);
        struct vfs_io r=vfs_write(fd,"must-fail",9);
        KASSERT(r.error==VFS_IO && !r.count && f->poisoned && f->volume.read_only);
        KASSERT(vfs_close(fd)==VFS_OK && vfs_sync("/mnt")==VFS_IO && vfs_unmount("/mnt")==VFS_IO);
        KASSERT(vfs_mkdir("/tmp/recovered")==VFS_OK);
        log_info("fat32-test","FAT32 WRITE ERROR PASS: volume isolado, sync falhou, kernel continua."); return;
    }
    if(mode==2) {
        KASSERT(f->geometry.free_clusters==0);
        int fd; KASSERT(vfs_open("/mnt/FULL.BIN",VFS_WRITE|VFS_APPEND,&fd)==VFS_OK);
        struct vfs_io r=vfs_write(fd,pattern,1024); KASSERT(r.error==VFS_NO_SPACE && !r.count);
        KASSERT(vfs_mkdir("/mnt/NOSPACE")==VFS_NO_SPACE);
        KASSERT(vfs_close(fd)==VFS_OK);
        write_file("/mnt/FILL.BIN","full-ok",7,VFS_WRITE);
        struct vfs_stat st; KASSERT(vfs_stat_path("/mnt/FILL.BIN",&st)==VFS_OK && st.size>=512);
        KASSERT(vfs_truncate("/mnt/FILL.BIN",st.size-512)==VFS_OK && f->geometry.free_clusters==1);
        write_file("/mnt/FULL.BIN",pattern,512,VFS_WRITE|VFS_APPEND);
        KASSERT(vfs_open("/mnt/FULL.BIN",VFS_WRITE|VFS_APPEND,&fd)==VFS_OK);
        r=vfs_write(fd,"x",1); KASSERT(r.error==VFS_NO_SPACE && !r.count && vfs_close(fd)==VFS_OK);
        KASSERT(vfs_truncate("/mnt/FULL.BIN",0)==VFS_OK && f->geometry.free_clusters==1);
        KASSERT(vfs_mkdir("/mnt/ONEFREE")==VFS_OK && vfs_remove("/mnt/ONEFREE",true)==VFS_OK);
        KASSERT(vfs_unmount("/mnt")==VFS_OK);
        log_info("fat32-test","FAT32 FULL PASS: sem sobrescrever, overwrite permitido e clusters reutilizados."); return;
    }
    for(size_t i=0;i<sizeof pattern;++i) pattern[i]=(uint8_t)(i*7+11);
    KASSERT(vfs_mkdir("/mnt/A")==VFS_OK && vfs_mkdir("/mnt/B")==VFS_OK);
    write_file("/mnt/A/INIT.BIN",pattern,sizeof pattern,VFS_CREATE|VFS_EXCLUSIVE|VFS_WRITE);
    KASSERT(vfs_truncate("/mnt/A/INIT.BIN",700)==VFS_OK && vfs_truncate("/mnt/A/INIT.BIN",1300)==VFS_OK);
    write_file("/mnt/A/INIT.BIN","TAIL",4,VFS_WRITE|VFS_APPEND);
    int fd; KASSERT(vfs_open("/mnt/A/INIT.BIN",VFS_READ,&fd)==VFS_OK);
    KASSERT(vfs_remove("/mnt/A/INIT.BIN",false)==VFS_BUSY && vfs_close(fd)==VFS_OK);
    KASSERT(vfs_rename("/mnt/A/INIT.BIN","/mnt/B/RESULT.BIN")==VFS_OK);
    KASSERT(vfs_rename("/mnt/B/RESULT.BIN","/tmp/other")==VFS_CROSS_DEVICE);
    KASSERT(vfs_mkdir("/mnt/A/SUB")==VFS_OK);
    write_file("/mnt/A/SUB/NOTE.TXT","moved-dir\n",10,VFS_CREATE|VFS_WRITE);
    KASSERT(vfs_rename("/mnt/A/SUB","/mnt/B/SUB")==VFS_OK && vfs_remove("/mnt/A",true)==VFS_OK);
    KASSERT(vfs_remove("/mnt/B",true)==VFS_NOT_EMPTY);
    write_file("/mnt/DELETE.TXT","deleted",7,VFS_CREATE|VFS_WRITE); KASSERT(vfs_remove("/mnt/DELETE.TXT",false)==VFS_OK);
    KASSERT(vfs_mkdir("/mnt/EMPTYDIR")==VFS_OK && vfs_remove("/mnt/EMPTYDIR",true)==VFS_OK);
    log_info("fat32-test","RW dados, append, truncate, move e rmdir: OK.");
    char aliases[2][13]; unsigned int found=0;
    KASSERT(vfs_open("/mnt",VFS_READ|VFS_OPEN_DIR,&fd)==VFS_OK);
    struct vfs_dirent entry; bool end;
    for(;;) {
        KASSERT(vfs_readdir(fd,&entry,&end)==VFS_OK); if(end) break;
        bool alias=false; for(size_t i=0;entry.name[i];++i) if(entry.name[i]=='~') alias=true;
        if(alias && found<2) strcpy(aliases[found++],entry.name);
    }
    KASSERT(found==2 && vfs_close(fd)==VFS_OK);
    char path[64]="/mnt/"; strcpy(path+5,aliases[0]); write_file(path,"more\n",5,VFS_WRITE|VFS_APPEND);
    KASSERT(vfs_rename(path,"/mnt/RENAMED.TXT")==VFS_OK);
    strcpy(path+5,aliases[1]); KASSERT(vfs_remove(path,false)==VFS_OK);
    KASSERT(vfs_mkdir("/mnt/GROW")==VFS_OK);
    for(unsigned int i=0;i<35;++i) {
        char name[]="/mnt/GROW/F00.TXT"; name[11]='0'+i/10; name[12]='0'+i%10;
        KASSERT(vfs_open(name,VFS_CREATE|VFS_WRITE,&fd)==VFS_OK && vfs_close(fd)==VFS_OK);
    }
    for(unsigned int i=0;i<35;++i) {
        char name[]="/mnt/GROW/F00.TXT"; name[11]='0'+i/10; name[12]='0'+i%10;
        KASSERT(vfs_remove(name,false)==VFS_OK);
    }
    KASSERT(vfs_remove("/mnt/GROW",true)==VFS_OK);
    KASSERT(vfs_sync("/mnt")==VFS_OK && f->geometry.clean);
    write_file("/mnt/AFTER.TXT","after-sync",10,VFS_CREATE|VFS_WRITE);
    KASSERT(!f->geometry.clean && vfs_remove("/mnt/AFTER.TXT",false)==VFS_OK);
    verify_result(); KASSERT(vfs_sync_all()==VFS_OK && f->geometry.clean && vfs_unmount("/mnt")==VFS_OK);
    log_info("fat32-test","FAT32 WRITE PASS: FATs, FSInfo, LFN, diretorios, sync e ciclos de reutilizacao OK.");
}
