#include <ata.h>

enum { ERR=1, DRQ=8, DF=32, BSY=128, POLL_LIMIT=1000000 };
struct deadline { uint64_t start, ticks; unsigned int polls; };
static struct block_result result(enum block_error e) { return (struct block_result){.error=e}; }
static uint8_t status(struct ata_channel *c) { return c->io.in8(c->io.context,c->control); }
static void delay(struct ata_channel *c) { for(unsigned int i=0;i<4;++i) (void)status(c); }
static void put(struct ata_channel *c,unsigned int reg,uint8_t v) { c->io.out8(c->io.context,c->command+reg,v); }
static struct deadline deadline(struct ata_channel *c,uint64_t ticks) {
    return (struct deadline){c->io.ticks(c->io.context),ticks,0};
}
static struct block_result fail(struct ata_channel *c,enum block_error e,uint8_t s) {
    struct block_result r=result(e); r.status_reg=s;
    if (s&ERR) r.error_reg=c->io.in8(c->io.context,c->command+1);
    if (e==BLOCK_TIMEOUT || e==BLOCK_IO) c->faulted=true;
    return r;
}
/* All waits in a transaction share one deadline, including the final status. */
static struct block_result wait_for(struct ata_channel *c,struct deadline *d,bool data,bool check_error) {
    for(;;) {
        uint8_t s=status(c);
        if (s==0 || s==255) return fail(c,BLOCK_IO,s);
        if (!(s&BSY)) {
            if (check_error && (s&(ERR|DF))) return fail(c,BLOCK_IO,s);
            if (!data || (s&DRQ)) return result(BLOCK_OK);
        }
        if (++d->polls>=POLL_LIMIT || c->io.ticks(c->io.context)-d->start>=d->ticks)
            return fail(c,BLOCK_TIMEOUT,s);
        c->io.relax(c->io.context);
    }
}
static struct block_result finish(struct ata_channel *c,struct deadline *d) {
    for(;;) {
        uint8_t s=status(c);
        if(s==0 || s==255) return fail(c,BLOCK_IO,s);
        if(!(s&BSY)) {
            if(s&(ERR|DF)) return fail(c,BLOCK_IO,s);
            if(!(s&DRQ)) return result(BLOCK_OK);
        }
        if (++d->polls>=POLL_LIMIT || c->io.ticks(c->io.context)-d->start>=d->ticks)
            return fail(c,BLOCK_TIMEOUT,s);
        c->io.relax(c->io.context);
    }
}
enum block_error ata_parse_identify(struct ata_drive *d,const uint16_t w[256]) {
    if (!(w[49]&(1u<<9))) return BLOCK_UNSUPPORTED;
    bool valid=(w[83]&0xc000)==0x4000;
    d->lba48=valid && (w[83]&(1u<<10));
    d->flush=valid && (w[83]&(1u<<12));
    d->flush_ext=d->lba48 && valid && (w[83]&(1u<<13));
    d->sectors=d->lba48 ? (uint64_t)w[100]|((uint64_t)w[101]<<16)|((uint64_t)w[102]<<32)|((uint64_t)w[103]<<48)
                            : (uint64_t)w[60]|((uint64_t)w[61]<<16);
    if (!d->sectors || d->sectors>(d->lba48 ? (1ull<<48) : (1ull<<28))) return BLOCK_BAD_DATA;
    d->sector_size=512;
    if ((w[106]&0xc000)==0x4000 && (w[106]&(1u<<12))) {
        uint32_t words=(uint32_t)w[117]|((uint32_t)w[118]<<16);
        if (words<=256 || words>BLOCK_MAX_SECTOR/2 || (words&(words-1))) return BLOCK_UNSUPPORTED;
        d->sector_size=words*2;
    }
    for(unsigned int i=0;i<40;++i) {
        char ch=(char)(w[27+i/2]>>(i%2 ? 0:8));
        d->model[i]=ch>=32 && ch<=126 ? ch : '?';
    }
    unsigned int n=40; while(n && d->model[n-1]==' ') --n; d->model[n]=0;
    return BLOCK_OK;
}
struct block_result ata_identify(struct ata_drive *d) {
    struct ata_channel *c=d->channel;
    if (c->faulted) return result(BLOCK_OFFLINE);
    c->io.out8(c->io.context,c->control,2); /* nIEN: polling, no ATA IRQs. */
    put(c,6,0xa0|(d->slave ? 16:0)); delay(c);
    uint8_t s=status(c);
    if (!s || s==255) return result(BLOCK_NO_DEVICE);
    struct deadline timeout=deadline(c,100);
    struct block_result r=wait_for(c,&timeout,false,false);
    if (r.error!=BLOCK_OK) return r;
    for(unsigned int i=2;i<=5;++i) put(c,i,0);
    put(c,7,0xec); delay(c);
    s=status(c); if (!s || s==255) return result(BLOCK_NO_DEVICE);
    r=wait_for(c,&timeout,false,false); if(r.error!=BLOCK_OK) return r;
    if(c->io.in8(c->io.context,c->command+4) || c->io.in8(c->io.context,c->command+5)) return result(BLOCK_UNSUPPORTED);
    r=wait_for(c,&timeout,true,true); if(r.error!=BLOCK_OK) return r;
    uint16_t words[256];
    for(unsigned int i=0;i<256;++i) words[i]=c->io.in16(c->io.context,c->command);
    delay(c); r=finish(c,&timeout); if(r.error!=BLOCK_OK) return r;
    return result(ata_parse_identify(d,words));
}
static struct block_result transfer(struct ata_drive *d,uint64_t lba,uint32_t count,void *buffer,bool write) {
    struct ata_channel *c=d->channel;
    if (c->faulted) return result(BLOCK_OFFLINE);
    if (!count || count>BLOCK_MAX_TRANSFER || !buffer || !d->sector_size) return result(BLOCK_ARGUMENT);
    uint64_t limit=d->lba48 ? 1ull<<48 : 1ull<<28;
    if (lba>=d->sectors || count>d->sectors-lba || lba>=limit || count>limit-lba) return result(BLOCK_RANGE);
    struct deadline timeout=deadline(c,300);
    put(c,6,0xe0|(d->slave ? 16:0)|(d->lba48 ? 0:(uint8_t)(lba>>24))); delay(c);
    struct block_result r=wait_for(c,&timeout,false,true); if(r.error!=BLOCK_OK) return r;
    if (d->lba48) {
        put(c,2,0); put(c,3,lba>>24); put(c,4,lba>>32); put(c,5,lba>>40);
    }
    put(c,2,count); put(c,3,lba); put(c,4,lba>>8); put(c,5,lba>>16);
    put(c,7,d->lba48 ? (write ? 0x34:0x24) : (write ? 0x30:0x20)); delay(c);
    uint8_t *bytes=buffer;
    uint32_t completed=0;
    for(;completed<count;++completed) {
        r=wait_for(c,&timeout,true,true); if(r.error!=BLOCK_OK) break;
        for(unsigned int j=0;j<d->sector_size;j+=2) {
            if(write) c->io.out16(c->io.context,c->command,(uint16_t)bytes[j]|((uint16_t)bytes[j+1]<<8));
            else { uint16_t word=c->io.in16(c->io.context,c->command); bytes[j]=word; bytes[j+1]=word>>8; }
        }
        bytes+=d->sector_size; delay(c);
    }
    if(r.error==BLOCK_OK) r=finish(c,&timeout);
    r.completed=completed; return r;
}
struct block_result ata_read(struct ata_drive *d,uint64_t lba,uint32_t count,void *buffer) { return transfer(d,lba,count,buffer,false); }
struct block_result ata_write(struct ata_drive *d,uint64_t lba,uint32_t count,const void *buffer) { return transfer(d,lba,count,(void *)buffer,true); }
struct block_result ata_flush(struct ata_drive *d) {
    struct ata_channel *c=d->channel;
    if(c->faulted) return result(BLOCK_OFFLINE);
    if(!d->flush && !d->flush_ext) return result(BLOCK_UNSUPPORTED);
    struct deadline timeout=deadline(c,500);
    put(c,6,0xe0|(d->slave ? 16:0)); delay(c);
    struct block_result r=wait_for(c,&timeout,false,true); if(r.error!=BLOCK_OK) return r;
    put(c,7,d->flush_ext ? 0xea:0xe7); delay(c);
    return finish(c,&timeout);
}
