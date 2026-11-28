#ifndef NEXUS_RAMFS_H
#define NEXUS_RAMFS_H
#include <vfs.h>
enum { RAMFS_NODES=32, RAMFS_FILE_SIZE=4096 };
struct ramfs_node {
    struct vfs_node node;
    bool used, borrowed;
    const uint8_t *boot_data;
    uint8_t data[RAMFS_FILE_SIZE];
};
struct ramfs { struct vfs_volume volume; struct ramfs_node nodes[RAMFS_NODES]; };
/* Initialize only when the volume is not mounted and no nodes are referenced. */
void ramfs_init(struct ramfs *);
/* Unmounted volume only. Bytes are borrowed, immutable and must stay mapped
 * until the volume is destroyed; this does NOT copy/reclaim boot memory. */
enum vfs_error ramfs_import_file(struct ramfs *,const char *,const void *,size_t);
#endif
