#ifndef NEXUS_FAT32_H
#define NEXUS_FAT32_H
#include <block.h>
#include <vfs.h>
enum { FAT32_NODES=128, FAT32_CHAIN_LIMIT=131072, FAT32_DIR_LIMIT=65536 };
enum fat32_error { FAT32_OK, FAT32_ARGUMENT, FAT32_GEOMETRY, FAT32_TYPE,
    FAT32_VERSION, FAT32_IO, FAT32_CORRUPT, FAT32_CYCLE, FAT32_LIMIT };
struct fat32_geometry {
    uint64_t start,length,total,fat_start,data_start;
    uint32_t sector_size,sectors_per_cluster,cluster_size,reserved,fat_count,fat_size,active_fat,clusters,root;
    bool mirrored,fsinfo_present,fsinfo_valid,free_known,next_known,clean,no_hard_error;
    uint32_t free_clusters,next_free;
};
struct fat32_node {
    struct vfs_node node;
    uint32_t first,chain_length,cursor,cursor_index;
    uint64_t entry_sector;
    unsigned int entry_offset;
};
struct fat32 {
    struct vfs_volume volume;
    struct fat32_geometry geometry;
    struct fat32_node nodes[FAT32_NODES];
    unsigned int node_count;
    enum fat32_error error;
    struct block_result block_error;
    bool ready,cache_valid;
    uint64_t cache_sector;
    uint8_t fat_cache[BLOCK_MAX_SECTOR],data[BLOCK_MAX_SECTOR];
};
/* Unmounted storage only, exclusive caller serialization. start/length are in
 * device sectors. Disk contents must remain unchanged for the mount lifetime. */
enum fat32_error fat32_open(struct fat32 *,const struct block_device *,uint64_t start,uint64_t length);
enum fat32_error fat32_chain(struct fat32 *,uint32_t first,uint32_t *length);
const char *fat32_error_name(enum fat32_error);
bool fat32_terminal_command(const char *,const char *);
void fat32_selftest(bool invalid,uint64_t start,uint64_t length);
#endif
