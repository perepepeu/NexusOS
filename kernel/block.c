#include <block.h>
static struct block_device devices[BLOCK_MAX_DEVICES];
static size_t device_count;
static struct block_result result(enum block_error error) { return (struct block_result){.error=error}; }
bool block_valid_device(const struct block_device *d) {
    return d && d->ops.read && d->sectors && d->sector_size>=512 && d->sector_size<=BLOCK_MAX_SECTOR &&
        !(d->sector_size & (d->sector_size-1)) && d->sectors<=UINT64_MAX/d->sector_size;
}
static enum block_error validate(const struct block_device *d,uint64_t lba,uint32_t count,const void *buffer,size_t bytes) {
    if (!block_valid_device(d)) return BLOCK_NO_DEVICE;
    if (count>BLOCK_MAX_TRANSFER) return BLOCK_ARGUMENT;
    if (lba>d->sectors || count>d->sectors-lba) return BLOCK_RANGE;
    if (!count) return BLOCK_OK;
    size_t required=(size_t)count*d->sector_size;
    if (!buffer || bytes<required || (uintptr_t)buffer>UINTPTR_MAX-required) return BLOCK_ARGUMENT;
    return BLOCK_OK;
}
struct block_result block_read(const struct block_device *d,uint64_t lba,uint32_t count,void *buffer,size_t bytes) {
    enum block_error e=validate(d,lba,count,buffer,bytes);
    if (e!=BLOCK_OK || !count) return result(e);
    return d->ops.read(d->context,lba,count,buffer);
}
struct block_result block_write(const struct block_device *d,uint64_t lba,uint32_t count,const void *buffer,size_t bytes) {
    enum block_error e=validate(d,lba,count,buffer,bytes);
    if (e!=BLOCK_OK || !count) return result(e);
    if (d->read_only) return result(BLOCK_READ_ONLY);
    if (!d->ops.write) return result(BLOCK_UNSUPPORTED);
    return d->ops.write(d->context,lba,count,buffer);
}
struct block_result block_flush(const struct block_device *d) {
    if (!block_valid_device(d)) return result(BLOCK_NO_DEVICE);
    if (d->read_only) return result(BLOCK_OK);
    return d->ops.flush ? d->ops.flush(d->context) : result(BLOCK_UNSUPPORTED);
}
bool block_register(const struct block_device *d) {
    if (!block_valid_device(d) || !d->name[0] || device_count==BLOCK_MAX_DEVICES) return false;
    size_t length=0; while(length<sizeof d->name && d->name[length]) ++length;
    if (length==sizeof d->name) return false;
    size_t model=0; while(model<sizeof d->model && d->model[model]) ++model;
    if (model==sizeof d->model) return false;
    for(size_t i=0;i<device_count;++i) {
        size_t j=0; while(d->name[j] && d->name[j]==devices[i].name[j]) ++j;
        if (!d->name[j] && !devices[i].name[j]) return false;
    }
    devices[device_count++]=*d; return true;
}
size_t block_count(void) { return device_count; }
const struct block_device *block_get(size_t index) { return index<device_count ? &devices[index] : 0; }
const char *block_error_name(enum block_error error) {
    static const char *const names[]={"OK","argumento invalido","LBA fora dos limites","somente leitura","nao suportado",
        "dispositivo ausente","timeout","erro de I/O","ocupado","canal isolado","contexto invalido","IDENTIFY invalido"};
    return (unsigned int)error<sizeof names/sizeof *names ? names[error] : "erro desconhecido";
}
