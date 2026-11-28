#include <ata.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fake {
    uint16_t identify[256];
    uint8_t regs[8], history[32][2], status, error, command;
    unsigned int entries, word, sectors, transferred, fail_after, words_per_sector, reads, writes, relaxes;
    uint64_t clock, selected_lba;
    bool stuck, stopped_clock, absent, packet, inject_error, final_error;
};
static uint8_t in8(void *p,uint16_t port) {
    struct fake *f=p;
    if(port==0x3f6) {
        ++f->reads;
        if(f->absent) return 0;
        if(f->stuck) return 128;
        return f->status;
    }
    if(port==0x1f1) return f->error;
    if(port==0x1f4 && f->packet) return 0x14;
    return f->regs[port-0x1f0];
}
static void next_word(struct fake *f) {
    if(++f->word!=f->words_per_sector) return;
    f->word=0; ++f->transferred;
    if(f->inject_error && f->transferred==f->fail_after) { f->status=0x41; f->error=0x40; }
    else if(f->transferred==f->sectors) f->status=f->final_error ? 0x61:0x40;
}
static uint16_t in16(void *p,uint16_t port) {
    struct fake *f=p; assert(port==0x1f0 && (f->status&8));
    uint16_t v=f->command==0xec ? f->identify[f->word] : (uint16_t)(f->word+f->transferred);
    next_word(f); return v;
}
static void out16(void *p,uint16_t port,uint16_t value) {
    (void)value; struct fake *f=p; assert(port==0x1f0 && (f->status&8)); ++f->writes; next_word(f);
}
static void out8(void *p,uint16_t port,uint8_t value) {
    struct fake *f=p;
    if(port==0x3f6) { assert(value==2); return; }
    unsigned int reg=port-0x1f0; assert(reg<8); f->regs[reg]=value;
    if(reg==6) { f->entries=0; return; }
    if(reg!=7) { assert(f->entries<32); f->history[f->entries][0]=reg; f->history[f->entries++][1]=value; return; }
    f->command=value; f->word=f->transferred=0; f->status=0x48;
    if(value==0xec) { f->sectors=1; f->words_per_sector=256; if(f->packet) f->status=0x41; return; }
    if(value==0xea || value==0xe7) { f->status=f->final_error ? 0x61:0x40; return; }
    bool ext=value==0x24 || value==0x34;
    assert(value==0x20 || value==0x30 || ext);
    assert(f->entries==(ext ? 8u:4u));
    for(unsigned int i=0;i<f->entries;++i) assert(f->history[i][0]==2+i%4);
    unsigned int base=ext ? 4:0;
    f->sectors=f->history[base][1];
    f->selected_lba=(uint64_t)f->regs[3]|((uint64_t)f->regs[4]<<8)|((uint64_t)f->regs[5]<<16);
    if(ext) f->selected_lba|=(uint64_t)f->history[1][1]<<24|(uint64_t)f->history[2][1]<<32|(uint64_t)f->history[3][1]<<40;
    else f->selected_lba|=(uint64_t)(f->regs[6]&15)<<24;
}
static uint64_t ticks(void *p) { return ((struct fake *)p)->clock; }
static void relax(void *p) { struct fake *f=p; ++f->relaxes; if(!f->stopped_clock) ++f->clock; }
static void setup(struct fake *f,struct ata_channel *c,struct ata_drive *d,bool extended) {
    memset(f,0,sizeof *f); f->status=0x40; f->words_per_sector=256;
    f->identify[49]=1<<9; f->identify[83]=0x4000|(1<<12)|(extended ? (1<<10)|(1<<13):0);
    f->identify[60]=1000; f->identify[100]=1000;
    for(unsigned int i=27;i<47;++i) f->identify[i]=0x2020;
    f->identify[27]=0x4142; f->identify[28]=0x4344;
    *c=(struct ata_channel){.io={in8,in16,out8,out16,ticks,relax,f},.command=0x1f0,.control=0x3f6};
    *d=(struct ata_drive){.channel=c};
    assert(ata_identify(d).error==BLOCK_OK && d->sectors==1000 && d->sector_size==512);
    assert(strcmp(d->model,"ABCD")==0 && d->lba48==extended);
}
int main(void) {
    struct fake f; struct ata_channel c; struct ata_drive d;
    uint8_t buffer[128*4096+1]; struct block_result r;
    for(unsigned int ext=0;ext<2;++ext) {
        setup(&f,&c,&d,ext); d.sectors=ext ? 1ull<<48 : 1ull<<28;
        uint64_t lba=ext ? 0x123456789abcull:0x0abcdef0ull;
        r=ata_read(&d,lba,2,buffer+1); assert(r.error==BLOCK_OK && r.completed==2 && f.selected_lba==lba);
        assert(buffer[1]==0 && buffer[3]==1 && buffer[513]==1);
        r=ata_write(&d,lba,128,buffer+1); assert(r.error==BLOCK_OK && r.completed==128 && f.writes==128*256);
        assert(ata_flush(&d).error==BLOCK_OK && f.command==(ext ? 0xea:0xe7));
        unsigned int before=f.reads;
        assert(ata_read(&d,d.sectors,1,buffer).error==BLOCK_RANGE && f.reads==before);
        assert(ata_write(&d,UINT64_MAX,2,buffer).error==BLOCK_RANGE);
        assert(ata_read(&d,0,129,buffer).error==BLOCK_ARGUMENT);
    }
    setup(&f,&c,&d,true); f.identify[106]=0x5000; f.identify[117]=2048;
    assert(ata_parse_identify(&d,f.identify)==BLOCK_OK && d.sector_size==4096);
    f.words_per_sector=2048; r=ata_read(&d,2,2,buffer+1); assert(r.error==BLOCK_OK && r.completed==2);
    f.identify[117]=257; assert(ata_parse_identify(&d,f.identify)==BLOCK_UNSUPPORTED);
    f.identify[117]=0; assert(ata_parse_identify(&d,f.identify)==BLOCK_UNSUPPORTED);
    f.identify[106]=0; f.identify[100]=0; assert(ata_parse_identify(&d,f.identify)==BLOCK_BAD_DATA);
    f.identify[103]=2; assert(ata_parse_identify(&d,f.identify)==BLOCK_BAD_DATA);
    setup(&f,&c,&d,false); f.identify[49]=0; assert(ata_parse_identify(&d,f.identify)==BLOCK_UNSUPPORTED);
    setup(&f,&c,&d,true); f.absent=true; assert(ata_identify(&d).error==BLOCK_NO_DEVICE && !c.faulted);
    setup(&f,&c,&d,true); f.packet=true; assert(ata_identify(&d).error==BLOCK_UNSUPPORTED && !c.faulted);
    setup(&f,&c,&d,true); f.inject_error=true; f.fail_after=1;
    r=ata_read(&d,0,2,buffer); assert(r.error==BLOCK_IO && r.completed==1 && r.status_reg==0x41 && r.error_reg==0x40 && c.faulted);
    assert(ata_flush(&d).error==BLOCK_OFFLINE && ata_read(&d,0,1,buffer).error==BLOCK_OFFLINE);
    setup(&f,&c,&d,true); f.final_error=true;
    r=ata_write(&d,0,1,buffer); assert(r.error==BLOCK_IO && r.completed==1 && c.faulted);
    setup(&f,&c,&d,true); f.final_error=true; assert(ata_flush(&d).error==BLOCK_IO);
    for(unsigned int stopped=0;stopped<2;++stopped) {
        setup(&f,&c,&d,true); f.stuck=true; f.stopped_clock=stopped; f.clock=UINT64_MAX-99;
        r=ata_read(&d,0,1,buffer); assert(r.error==BLOCK_TIMEOUT && !r.completed && c.faulted && f.relaxes<=1000000);
        assert(stopped ? f.relaxes==999999 : f.relaxes==300);
    }
    setup(&f,&c,&d,true); d.flush=d.flush_ext=false; assert(ata_flush(&d).error==BLOCK_UNSUPPORTED);
    puts("OK: ATA IDENTIFY, LBA28/48, PIO 512/4096, unaligned, flush, parciais, erros, timeout e isolamento de canal.");
}
