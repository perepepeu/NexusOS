#ifndef NEXUS_ATA_H
#define NEXUS_ATA_H
#include <block.h>

/* Injectable port operations keep the protocol independent of privileged I/O. */
struct ata_io {
    uint8_t (*in8)(void *, uint16_t);
    uint16_t (*in16)(void *, uint16_t);
    void (*out8)(void *, uint16_t, uint8_t);
    void (*out16)(void *, uint16_t, uint16_t);
    uint64_t (*ticks)(void *);
    void (*relax)(void *);
    void *context;
};
struct ata_channel {
    struct ata_io io;
    uint16_t command, control;
    bool faulted;
};
struct ata_drive {
    struct ata_channel *channel;
    bool slave, lba48, flush, flush_ext;
    uint64_t sectors;
    uint32_t sector_size;
    char model[41];
};
enum block_error ata_parse_identify(struct ata_drive *, const uint16_t words[256]);
struct block_result ata_identify(struct ata_drive *);
struct block_result ata_read(struct ata_drive *, uint64_t, uint32_t, void *);
struct block_result ata_write(struct ata_drive *, uint64_t, uint32_t, const void *);
struct block_result ata_flush(struct ata_drive *);
void storage_init(void);
void storage_list(void);
void storage_selftest(bool verify);
#endif
