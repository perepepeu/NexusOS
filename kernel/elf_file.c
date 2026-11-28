#include <programs.h>
#include <process.h>
#include <heap.h>
#include <runtime.h>
uint64_t process_create_elf_file(const char *path,enum elf64_result *elf_error,enum vfs_error *vfs_error) {
    enum elf64_result result=ELF64_ARGUMENT; enum vfs_error io=VFS_INVALID;
    uint64_t pid=0; void *image=0; int fd=-1;
    if(!path || !*path) goto done;
    process_preempt_disable();
    struct vfs_stat st; io=vfs_stat_path(path,&st); if(io) goto unlock;
    if(st.type!=VFS_FILE) { io=VFS_IS_DIR; goto unlock; }
    if(st.size>ELF64_MAX_FILE) { result=ELF64_LIMIT; goto unlock; }
    if(st.size<sizeof(struct elf64_header)) { result=ELF64_TRUNCATED; goto unlock; }
    const char *name=path; for(const char *p=path;*p;++p) if(*p=='/') name=p+1;
    if(!*name || strlen(name)>ELF64_ARG0_MAX) { io=VFS_NAME_LONG; goto unlock; }
    io=vfs_open(path,VFS_READ,&fd); if(io) goto unlock;
    image=kmalloc((size_t)st.size); if(!image) { result=ELF64_MEMORY; goto unlock; }
    for(size_t offset=0;offset<st.size;) {
        size_t count=st.size-offset; if(count>4096) count=4096;
        struct vfs_io r=vfs_read(fd,(unsigned char *)image+offset,count);
        if(r.error || !r.count) { io=r.error ? r.error:VFS_IO; result=ELF64_IO; goto unlock; }
        offset+=r.count;
    }
    io=vfs_close(fd); fd=-1; if(io) { result=ELF64_IO; goto unlock; }
    pid=process_create_elf(name,image,(size_t)st.size,&result);
unlock:
    if(fd>=0) { enum vfs_error closed=vfs_close(fd); if(!io) io=closed; }
    kfree(image); process_preempt_enable();
done:
    if(elf_error) *elf_error=result;
    if(vfs_error) *vfs_error=io;
    return pid;
}
