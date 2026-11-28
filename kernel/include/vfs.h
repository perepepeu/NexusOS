#ifndef NEXUS_VFS_H
#define NEXUS_VFS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { VFS_NAME_MAX=31, VFS_PATH_MAX=256, VFS_MOUNTS=8, VFS_PROCESSES=32,
    VFS_FDS=32, VFS_FIRST_FD=3, VFS_IO_MAX=65536 };
enum vfs_error { VFS_OK, VFS_INVALID, VFS_NOT_FOUND, VFS_EXISTS, VFS_NOT_DIR,
    VFS_IS_DIR, VFS_NAME_LONG, VFS_NO_SPACE, VFS_BAD_FD, VFS_ACCESS, VFS_BUSY,
    VFS_READ_ONLY, VFS_UNSUPPORTED, VFS_IO, VFS_CONTEXT };
enum vfs_type { VFS_FILE, VFS_DIRECTORY };
enum vfs_flags { VFS_READ=1, VFS_WRITE=2, VFS_CREATE=4, VFS_EXCLUSIVE=8,
    VFS_TRUNCATE=16, VFS_APPEND=32, VFS_OPEN_DIR=64 };
enum vfs_whence { VFS_SEEK_SET, VFS_SEEK_CUR, VFS_SEEK_END };
struct vfs_io { enum vfs_error error; size_t count; };
struct vfs_stat { enum vfs_type type; uint64_t size; };
struct vfs_dirent { char name[VFS_NAME_MAX+1]; enum vfs_type type; uint64_t size; };
struct vfs_volume;
struct vfs_node {
    struct vfs_volume *volume;
    struct vfs_node *parent;
    enum vfs_type type;
    char name[VFS_NAME_MAX+1];
    uint64_t size;
};
/* Backend owns stable nodes until unmount; no per-open backend resources.
 * Callbacks run under the VFS lock, with IRQs enabled. Must not yield,
 * recursively call VFS, or store caller buffers. count <= requested ALWAYS. */
struct vfs_ops {
    enum vfs_error (*lookup)(struct vfs_node *,const char *,struct vfs_node **);
    enum vfs_error (*create)(struct vfs_node *,const char *,enum vfs_type,struct vfs_node **);
    struct vfs_io (*read)(struct vfs_node *,uint64_t,void *,size_t);
    struct vfs_io (*write)(struct vfs_node *,uint64_t,const void *,size_t);
    enum vfs_error (*truncate)(struct vfs_node *);
    enum vfs_error (*readdir)(struct vfs_node *,uint64_t,struct vfs_dirent *,bool *);
    enum vfs_error (*sync)(struct vfs_volume *);
};
struct block_device;
struct vfs_volume {
    const struct vfs_ops *ops;
    struct vfs_node *root;
    void *context;
    const struct block_device *device; /* Optional; filesystem interprets blocks. */
    bool read_only;
};
struct vfs_stats { unsigned int mounts, sessions, descriptors; };

bool vfs_init(void);
enum vfs_error vfs_open(const char *,unsigned int,int *);
enum vfs_error vfs_close(int);
struct vfs_io vfs_read(int,void *,size_t);
struct vfs_io vfs_write(int,const void *,size_t);
enum vfs_error vfs_seek(int,int64_t,enum vfs_whence,uint64_t *);
enum vfs_error vfs_readdir(int,struct vfs_dirent *,bool *);
enum vfs_error vfs_stat_path(const char *,struct vfs_stat *);
enum vfs_error vfs_mkdir(const char *);
enum vfs_error vfs_chdir(const char *);
enum vfs_error vfs_getcwd(char *,size_t);
enum vfs_error vfs_mount(const char *,struct vfs_volume *);
enum vfs_error vfs_unmount(const char *);
void vfs_get_stats(struct vfs_stats *);
/* Scheduler-only: IRQs/preemption disabled, no backend I/O or callbacks. */
void vfs_process_reap(uint64_t);
const char *vfs_error_name(enum vfs_error);
void vfs_selftest(void);
bool vfs_terminal_command(const char *,const char *);
#endif
