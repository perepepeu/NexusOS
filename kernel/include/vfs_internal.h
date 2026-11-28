#ifndef NEXUS_VFS_INTERNAL_H
#define NEXUS_VFS_INTERNAL_H
#include <vfs.h>
struct vfs_location { unsigned int mount; struct vfs_node *node; };
struct vfs_file { bool used; unsigned int flags; uint64_t offset; struct vfs_location location; };
struct vfs_session {
    uint64_t pid;
    struct vfs_location cwd;
    struct vfs_file files[VFS_FDS];
};
struct vfs_mountpoint {
    bool used;
    struct vfs_volume *volume;
    struct vfs_location covered;
    unsigned int users;
};
struct vfs {
    bool ready;
    struct vfs_mountpoint mounts[VFS_MOUNTS];
    struct vfs_session sessions[VFS_PROCESSES];
};
/* Pure core: caller serializes every operation, including reaping. */
enum vfs_error vfs_core_init(struct vfs *,struct vfs_volume *);
enum vfs_error vfs_core_open(struct vfs *,uint64_t,const char *,unsigned int,int *);
enum vfs_error vfs_core_close(struct vfs *,uint64_t,int);
struct vfs_io vfs_core_read(struct vfs *,uint64_t,int,void *,size_t);
struct vfs_io vfs_core_write(struct vfs *,uint64_t,int,const void *,size_t);
enum vfs_error vfs_core_seek(struct vfs *,uint64_t,int,int64_t,enum vfs_whence,uint64_t *);
enum vfs_error vfs_core_readdir(struct vfs *,uint64_t,int,struct vfs_dirent *,bool *);
enum vfs_error vfs_core_stat(struct vfs *,uint64_t,const char *,struct vfs_stat *);
enum vfs_error vfs_core_mkdir(struct vfs *,uint64_t,const char *);
enum vfs_error vfs_core_chdir(struct vfs *,uint64_t,const char *);
enum vfs_error vfs_core_getcwd(struct vfs *,uint64_t,char *,size_t);
enum vfs_error vfs_core_mount(struct vfs *,uint64_t,const char *,struct vfs_volume *);
enum vfs_error vfs_core_unmount(struct vfs *,uint64_t,const char *);
enum vfs_error vfs_core_truncate(struct vfs *,uint64_t,const char *,uint64_t);
enum vfs_error vfs_core_rename(struct vfs *,uint64_t,const char *,const char *);
enum vfs_error vfs_core_remove(struct vfs *,uint64_t,const char *,bool);
enum vfs_error vfs_core_sync(struct vfs *,uint64_t,const char *);
enum vfs_error vfs_core_sync_all(struct vfs *);
void vfs_core_reap(struct vfs *,uint64_t);
void vfs_core_stats(const struct vfs *,struct vfs_stats *);
#endif
