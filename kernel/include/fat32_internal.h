#ifndef NEXUS_FAT32_INTERNAL_H
#define NEXUS_FAT32_INTERNAL_H
#include <fat32.h>
enum fat32_error fat32_set_error(struct fat32 *,enum fat32_error);
enum fat32_error fat32_read_sector(struct fat32 *,uint64_t,uint8_t *);
enum fat32_error fat32_get_fat(struct fat32 *,uint32_t,uint32_t *);
enum fat32_error fat32_next(struct fat32 *,uint32_t,uint32_t *,bool *);
struct fat32_node *fat32_node_slot(struct fat32 *);
uint8_t *fat32_tx_find(struct fat32 *,uint64_t);
enum vfs_error fat32_create(struct vfs_node *,const char *,enum vfs_type,struct vfs_node **);
struct vfs_io fat32_write(struct vfs_node *,uint64_t,const void *,size_t);
enum vfs_error fat32_resize(struct vfs_node *,uint64_t);
enum vfs_error fat32_truncate(struct vfs_node *);
enum vfs_error fat32_rename(struct vfs_node *,struct vfs_node *,const char *);
enum vfs_error fat32_remove(struct vfs_node *);
enum vfs_error fat32_sync(struct vfs_volume *);
bool fat32_rtc(uint16_t *,uint16_t *);
enum vfs_error fat32_mount_runtime(const struct block_device *,const char *,uint64_t,uint64_t,bool,struct fat32 **,enum fat32_error *);
#endif
