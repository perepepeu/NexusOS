#include <block.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned int reads,writes,flushes;
static struct block_result read_blocks(void *ctx,uint64_t lba,uint32_t count,void *data) {
    assert(ctx==(void *)17 && lba+count<=100); ++reads;
    memset(data,0x5a,(size_t)count*512); return (struct block_result){.completed=count};
}
static struct block_result write_blocks(void *ctx,uint64_t lba,uint32_t count,const void *data) {
    assert(ctx==(void *)17 && lba+count<=100 && *(const unsigned char *)data==0x5a); ++writes;
    return (struct block_result){.error=BLOCK_IO,.completed=count-1,.status_reg=0x41,.error_reg=0x40};
}
static struct block_result flush(void *ctx) { assert(ctx==(void *)17); ++flushes; return (struct block_result){0}; }
int main(void) {
    struct block_device d={.name="test",.sectors=100,.sector_size=512,.context=(void *)17,
        .ops={read_blocks,write_blocks,flush}};
    unsigned char data[1024];
    assert(block_valid_device(&d));
    assert(block_read(&d,99,1,data,sizeof data).completed==1 && reads==1);
    assert(block_read(&d,98,2,data,sizeof data).completed==2 && reads==2);
    assert(block_read(&d,100,1,data,sizeof data).error==BLOCK_RANGE);
    assert(block_read(&d,UINT64_MAX,2,data,sizeof data).error==BLOCK_RANGE);
    assert(block_read(&d,99,2,data,sizeof data).error==BLOCK_RANGE);
    assert(block_read(&d,0,129,data,sizeof data).error==BLOCK_ARGUMENT);
    assert(block_read(&d,0,2,data,1023).error==BLOCK_ARGUMENT);
    assert(block_read(&d,0,1,0,512).error==BLOCK_ARGUMENT);
    assert(block_read(&d,0,1,(void *)(UINTPTR_MAX-2),512).error==BLOCK_ARGUMENT);
    assert(block_read(&d,100,0,0,0).error==BLOCK_OK && reads==2);
    assert(block_read(&d,101,0,0,0).error==BLOCK_RANGE);
    struct block_result r=block_write(&d,0,2,data,sizeof data);
    assert(r.error==BLOCK_IO && r.completed==1 && r.error_reg==0x40 && writes==1);
    d.read_only=true; assert(block_write(&d,0,1,data,512).error==BLOCK_READ_ONLY && writes==1);
    assert(block_flush(&d).error==BLOCK_OK && !flushes); d.read_only=false;
    assert(block_flush(&d).error==BLOCK_OK && flushes==1);
    d.ops.flush=0; assert(block_flush(&d).error==BLOCK_UNSUPPORTED); d.ops.flush=flush;
    d.sector_size=513; assert(!block_valid_device(&d)); d.sector_size=4096;
    d.sectors=UINT64_MAX; assert(!block_valid_device(&d)); d.sectors=100; d.sector_size=512;
    assert(block_register(&d) && !block_register(&d));
    for(unsigned int i=1;i<BLOCK_MAX_DEVICES;++i) { d.name[0]=(char)('a'+i); assert(block_register(&d)); }
    assert(!block_register(&d) && block_count()==BLOCK_MAX_DEVICES && !block_get(BLOCK_MAX_DEVICES));
    assert(!strcmp(block_get(0)->name,"test"));
    assert(block_read(0,0,1,data,512).error==BLOCK_NO_DEVICE);
    puts("OK: bloco generico, capacidade, limites LBA/overflow/buffer, readonly, flush, parciais e catalogo.");
}
