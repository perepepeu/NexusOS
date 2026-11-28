#include <syscall.h>
#include <vfs.h>
#include <runtime.h>
#include <process.h>
_Static_assert(USER_PATH_MAX==VFS_PATH_MAX,"path ABI");
_Static_assert(USER_O_READ==VFS_READ && USER_O_WRITE==VFS_WRITE && USER_O_CREATE==VFS_CREATE &&
    USER_O_EXCLUSIVE==VFS_EXCLUSIVE && USER_O_TRUNCATE==VFS_TRUNCATE && USER_O_APPEND==VFS_APPEND && USER_O_DIRECTORY==VFS_OPEN_DIR,"flags ABI");
static int64_t error(enum vfs_error e) {
    static const unsigned int errors[]={0,USER_EINVAL,USER_ENOENT,USER_EEXIST,USER_ENOTDIR,USER_EISDIR,
        USER_ENAMETOOLONG,USER_ENOSPC,USER_EBADF,USER_EBADF,USER_EBUSY,USER_EROFS,USER_ENOSYS,USER_EIO,
        USER_EINVAL,USER_ENOTEMPTY,USER_EXDEV};
    return -(int64_t)((unsigned int)e<sizeof errors/sizeof *errors ? errors[e]:USER_EIO);
}
/* Copy through the page-aware user interface, including the terminating NUL.
 * No VFS call (not even O_TRUNC) until the complete path has been validated. */
static int64_t path_copy(struct address_space *space,uint64_t user,char out[VFS_PATH_MAX]) {
    for(size_t i=0;i<VFS_PATH_MAX;++i) {
        if(!address_user_copy(space,user+i,out+i,1,false)) return -USER_EFAULT;
        if(!out[i]) return i ? 0:-USER_ENOENT;
    }
    return -USER_ENAMETOOLONG;
}
int64_t syscall_file_io(struct address_space *space,uint64_t fd,uint64_t user,uint64_t bytes,bool write) {
    if(fd<VFS_FIRST_FD || fd>=VFS_FDS) return -USER_EBADF;
    process_preempt_disable();
    struct vfs_io probe=write ? vfs_write((int)fd,0,0):vfs_read((int)fd,0,0);
    int64_t result=error(probe.error);
    if(!result && bytes>USER_IO_MAX) result=-USER_EINVAL;
    if(!result && bytes && !address_user_check(space,user,(size_t)bytes,!write)) result=-USER_EFAULT;
    if(!result && bytes) {
        unsigned char bounce[USER_IO_MAX];
        if(write && !address_user_copy(space,user,bounce,(size_t)bytes,false)) result=-USER_EFAULT;
        else {
            struct vfs_io r=write ? vfs_write((int)fd,bounce,(size_t)bytes):vfs_read((int)fd,bounce,(size_t)bytes);
            if(!write && r.count && !address_user_copy(space,user,bounce,r.count,true)) result=-USER_EFAULT;
            else result=r.count ? (int64_t)r.count:error(r.error);
        }
    }
    process_preempt_enable(); return result;
}
int64_t sys_open(struct address_space *space,uint64_t user,uint64_t flags) {
    if(flags>127) return -USER_EINVAL;
    process_preempt_disable(); char path[VFS_PATH_MAX]; int fd;
    int64_t result=path_copy(space,user,path);
    if(!result) { enum vfs_error e=vfs_open(path,(unsigned int)flags,&fd); result=e ? error(e):fd; }
    process_preempt_enable(); return result;
}
int64_t sys_close(uint64_t fd) { return fd<VFS_FIRST_FD || fd>=VFS_FDS ? -USER_EBADF:error(vfs_close((int)fd)); }
int64_t sys_seek(uint64_t fd,int64_t offset,uint64_t whence) {
    if(fd<VFS_FIRST_FD || fd>=VFS_FDS) return -USER_EBADF;
    if(whence>USER_SEEK_END) return -USER_EINVAL;
    uint64_t position; enum vfs_error e=vfs_seek((int)fd,offset,(enum vfs_whence)whence,&position);
    return e ? error(e):(int64_t)position;
}
int64_t sys_stat(struct address_space *space,uint64_t user,uint64_t out) {
    process_preempt_disable(); int64_t result=0;
    if(!address_user_check(space,out,sizeof(struct user_stat),true)) result=-USER_EFAULT;
    char path[VFS_PATH_MAX]; if(!result) result=path_copy(space,user,path);
    if(!result) {
        struct vfs_stat st; enum vfs_error e=vfs_stat_path(path,&st); result=error(e);
        if(!e) {
            struct user_stat copy={.size=st.size,.type=st.type==VFS_DIRECTORY ? USER_DT_DIR:USER_DT_FILE};
            if(!address_user_copy(space,out,&copy,sizeof copy,true)) result=-USER_EFAULT;
        }
    }
    process_preempt_enable(); return result;
}
static int64_t path_op(struct address_space *space,uint64_t user,unsigned int op) {
    process_preempt_disable(); char path[VFS_PATH_MAX]; int64_t result=path_copy(space,user,path);
    if(!result) {
        enum vfs_error e=op==0 ? vfs_mkdir(path):op==1 ? vfs_remove(path,false):op==2 ? vfs_remove(path,true):
            op==3 ? vfs_chdir(path):vfs_sync(path);
        result=error(e);
    }
    process_preempt_enable(); return result;
}
int64_t sys_mkdir(struct address_space *space,uint64_t path) { return path_op(space,path,0); }
int64_t sys_unlink(struct address_space *space,uint64_t path,uint64_t flags) {
    return flags>USER_UNLINK_DIR ? -USER_EINVAL:path_op(space,path,flags ? 2:1);
}
int64_t sys_chdir(struct address_space *space,uint64_t path) { return path_op(space,path,3); }
int64_t sys_sync(struct address_space *space,uint64_t path) { return path ? path_op(space,path,4):error(vfs_sync_all()); }
int64_t sys_getcwd(struct address_space *space,uint64_t out,uint64_t bytes) {
    if(!bytes || bytes>USER_IO_MAX) return -USER_EINVAL;
    process_preempt_disable(); int64_t result=0;
    if(!address_user_check(space,out,(size_t)bytes,true)) result=-USER_EFAULT;
    if(!result) {
        char path[VFS_PATH_MAX]; size_t capacity=bytes<sizeof path ? (size_t)bytes:sizeof path;
        enum vfs_error e=vfs_getcwd(path,capacity); result=error(e);
        if(!e) {
            size_t count=strlen(path)+1;
            result=address_user_copy(space,out,path,count,true) ? (int64_t)count:-USER_EFAULT;
        }
    }
    process_preempt_enable(); return result;
}
int64_t sys_readdir(struct address_space *space,uint64_t fd,uint64_t out) {
    if(fd<VFS_FIRST_FD || fd>=VFS_FDS) return -USER_EBADF;
    process_preempt_disable(); int64_t result=0;
    if(!address_user_check(space,out,sizeof(struct user_dirent),true)) result=-USER_EFAULT;
    if(!result) {
        struct vfs_dirent entry; bool end; enum vfs_error e=vfs_readdir((int)fd,&entry,&end); result=error(e);
        if(!e && !end) {
            struct user_dirent copy={.size=entry.size,.type=entry.type==VFS_DIRECTORY ? USER_DT_DIR:USER_DT_FILE};
            size_t length=0; while(length<sizeof entry.name && entry.name[length]) ++length;
            if(length==sizeof entry.name || (entry.type!=VFS_DIRECTORY && entry.type!=VFS_FILE)) result=-USER_EIO;
            else {
                memcpy(copy.name,entry.name,length+1);
                result=address_user_copy(space,out,&copy,sizeof copy,true) ? 1:-USER_EFAULT;
            }
        }
    }
    process_preempt_enable(); return result;
}
