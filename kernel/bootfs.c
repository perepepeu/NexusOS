#include <bootfs.h>
#include <runtime.h>
#include <log.h>
#include <panic.h>
static struct ramfs fs;
static struct bootfs_report report;
static bool ready;
bool bootfs_init(const struct limine_module_response *response) {
    if(ready) return false;
    bootfs_populate(&fs,response,&report);
    if(vfs_mkdir("/boot") || vfs_mkdir("/boot/modules") || vfs_mount("/boot/modules",&fs.volume)) return false;
    ready=true;
    log_info("bootfs","/boot/modules: %u modulo(s), %u rejeitado(s), somente leitura.",report.imported,report.rejected);
    if(report.truncated) log_warn("bootfs","Limite de inspecao: primeiros 64 modulos.");
    return true;
}
void bootfs_selftest(void) {
    KASSERT(ready); struct vfs_stats before,after; vfs_get_stats(&before);
    unsigned int files=0;
    for(unsigned int i=1;i<RAMFS_NODES;++i) if(fs.nodes[i].used) {
        const struct ramfs_node *n=&fs.nodes[i]; char path[VFS_PATH_MAX]="/boot/modules/";
        strcpy(path+strlen(path),n->node.name);
        struct vfs_stat st; KASSERT(vfs_stat_path(path,&st)==VFS_OK && st.type==VFS_FILE && st.size==n->node.size);
        int fd; KASSERT(vfs_open(path,VFS_READ,&fd)==VFS_OK);
        uint8_t buffer[512]; size_t offset=0; uint32_t hash=2166136261u;
        for(;;) {
            struct vfs_io r=vfs_read(fd,buffer,sizeof buffer); KASSERT(!r.error);
            if(!r.count) break;
            KASSERT(offset+r.count<=n->node.size && !memcmp(buffer,n->boot_data+offset,r.count));
            for(size_t j=0;j<r.count;++j) hash=(hash^buffer[j])*16777619u;
            offset+=r.count;
        }
        KASSERT(offset==n->node.size);
        if(offset) {
            uint64_t position; KASSERT(vfs_seek(fd,-1,VFS_SEEK_END,&position)==VFS_OK && position==offset-1);
            struct vfs_io r=vfs_read(fd,buffer,sizeof buffer); KASSERT(!r.error && r.count==1 && buffer[0]==n->boot_data[offset-1]);
        }
        KASSERT(vfs_unmount("/boot/modules")==VFS_BUSY && vfs_close(fd)==VFS_OK);
        KASSERT(vfs_open(path,VFS_WRITE,&fd)==VFS_READ_ONLY);
        KASSERT(vfs_open(path,VFS_WRITE|VFS_TRUNCATE,&fd)==VFS_READ_ONLY);
        log_info("bootfs-test","BOOTFS FILE %s bytes=%llu hash=%x",n->node.name,(unsigned long long)offset,hash); ++files;
    }
    KASSERT(files==report.imported);
    int fd; KASSERT(vfs_open("/boot/modules/new",VFS_WRITE|VFS_CREATE,&fd)==VFS_READ_ONLY);
    KASSERT(vfs_mkdir("/boot/modules/newdir")==VFS_READ_ONLY);
    KASSERT(vfs_open("/boot/modules",VFS_READ|VFS_OPEN_DIR,&fd)==VFS_OK);
    unsigned int entries=0; bool end=false; struct vfs_dirent entry;
    for(;;) { KASSERT(vfs_readdir(fd,&entry,&end)==VFS_OK); if(end) break; ++entries; }
    KASSERT(entries==files && vfs_close(fd)==VFS_OK);
    vfs_get_stats(&after); KASSERT(before.mounts==after.mounts && before.sessions==after.sessions && before.descriptors==after.descriptors);
    log_info("bootfs-test","BOOTFS SELFTEST PASS: arquivos=%u, conteudo integral e readonly; sem FDs vazados.",files);
}
