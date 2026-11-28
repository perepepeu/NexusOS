#ifndef NEXUS_BLOCK_H
#define NEXUS_BLOCK_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
enum { BLOCK_MAX_DEVICES=8, BLOCK_MAX_TRANSFER=128, BLOCK_MAX_SECTOR=4096 };
enum block_error { BLOCK_OK, BLOCK_ARGUMENT, BLOCK_RANGE, BLOCK_READ_ONLY, BLOCK_UNSUPPORTED,
    BLOCK_NO_DEVICE, BLOCK_TIMEOUT, BLOCK_IO, BLOCK_BUSY, BLOCK_OFFLINE, BLOCK_CONTEXT, BLOCK_BAD_DATA };
struct block_result { enum block_error error; uint32_t completed; uint8_t status_reg, error_reg; };
struct block_device;
struct block_ops {
    struct block_result (*read)(void *,uint64_t,uint32_t,void *);
    struct block_result (*write)(void *,uint64_t,uint32_t,const void *);
    struct block_result (*flush)(void *);
};
struct block_device {
    char name[16], model[41]; uint64_t sectors; uint32_t sector_size;
    bool read_only; struct block_ops ops; void *context;
};
bool block_valid_device(const struct block_device *);
struct block_result block_read(const struct block_device *,uint64_t lba,uint32_t count,void *buffer,size_t bytes);
struct block_result block_write(const struct block_device *,uint64_t lba,uint32_t count,const void *buffer,size_t bytes);
struct block_result block_flush(const struct block_device *);
const char *block_error_name(enum block_error);
/* Catalogo preenchido somente durante storage_init, depois somente leitura. */
bool block_register(const struct block_device *);
size_t block_count(void);
const struct block_device *block_get(size_t index);
#endif
