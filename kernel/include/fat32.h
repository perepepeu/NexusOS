#ifndef NEXUS_FAT32_H
#define NEXUS_FAT32_H
#include <block.h>
#include <vfs.h>
enum { FAT32_NODES=128, FAT32_CHAIN_LIMIT=131072, FAT32_DIR_LIMIT=65536,
    FAT32_TX_SECTORS=64, FAT32_TX_CLUSTERS=256, FAT32_WRITE_CLUSTERS=1048576 };
enum fat32_error { FAT32_OK, FAT32_ARGUMENT, FAT32_GEOMETRY, FAT32_TYPE,
    FAT32_VERSION, FAT32_IO, FAT32_CORRUPT, FAT32_CYCLE, FAT32_LIMIT, FAT32_FULL, FAT32_READ_ONLY, FAT32_DIRTY };
struct fat32_geometry {
    uint64_t start,length,total,fat_start,data_start;
    uint32_t sector_size,sectors_per_cluster,cluster_size,reserved,fat_count,fat_size,active_fat,clusters,root;
    bool mirrored,fsinfo_present,fsinfo_valid,free_known,next_known,clean,no_hard_error;
    uint32_t free_clusters,next_free;
    uint16_t info_sector,backup_sector;
};
struct fat32_node {
    struct vfs_node node;
    uint32_t first,chain_length,cursor,cursor_index;
    uint64_t entry_sector;
    unsigned int entry_offset;
    uint32_t entry_index;
    uint8_t attributes,lfn_count;
    bool used,lfn_unsafe;
};
struct fat32_tx_sector { uint64_t relative; uint8_t bytes[BLOCK_MAX_SECTOR]; };
struct fat32_transaction {
    bool active;
    unsigned int count,allocated_count;
    uint32_t allocated[FAT32_TX_CLUSTERS],free_after,next_after;
    struct fat32_tx_sector sectors[FAT32_TX_SECTORS];
};
struct fat32 {
    struct vfs_volume volume;
    struct fat32_geometry geometry;
    struct fat32_node nodes[FAT32_NODES];
    unsigned int node_count;
    enum fat32_error error;
    struct block_result block_error;
    bool ready,cache_valid,writable,poisoned;
    uint64_t cache_sector;
    uint8_t fat_cache[BLOCK_MAX_SECTOR],data[BLOCK_MAX_SECTOR];
    struct fat32_transaction tx;
    bool (*clock)(uint16_t *date,uint16_t *time);
    void (*checkpoint)(struct fat32 *,unsigned int phase); /* Kernel test hook; normally NULL. */
};
/* Unmounted storage only, exclusive caller serialization. start/length are in
 * device sectors. External changes are forbidden for the mount lifetime.
 * fat32_open starts readonly; enable_write explicitly authorizes mutations. */
enum fat32_error fat32_open(struct fat32 *,const struct block_device *,uint64_t start,uint64_t length);
enum fat32_error fat32_chain(struct fat32 *,uint32_t first,uint32_t *length);
enum fat32_error fat32_enable_write(struct fat32 *,bool (*clock)(uint16_t *,uint16_t *));
void fat32_write_selftest(unsigned int mode);
const char *fat32_error_name(enum fat32_error);
bool fat32_terminal_command(const char *,const char *);
void fat32_selftest(bool invalid,uint64_t start,uint64_t length);
#endif
