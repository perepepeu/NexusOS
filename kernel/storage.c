#include <ata.h>
#include <io.h>
#include <interrupts.h>
#include <process.h>
#include <log.h>
#include <runtime.h>

static struct ata_channel channels[4];
static struct ata_drive drives[BLOCK_MAX_DEVICES];
static unsigned int channel_count, drive_count;
static bool initialized, busy;
static uint8_t read8(void *p,uint16_t port) { (void)p; return inb(port); }
static uint16_t read16(void *p,uint16_t port) { (void)p; return inw(port); }
static void write8(void *p,uint16_t port,uint8_t value) { (void)p; outb(port,value); }
static void write16(void *p,uint16_t port,uint16_t value) { (void)p; outw(port,value); }
static uint64_t ticks(void *p) { (void)p; return timer_ticks(); }
static void relax(void *p) { (void)p; __asm__ volatile("hlt" : : : "memory"); }
static bool enabled(void) {
    uint64_t flags; __asm__ volatile("pushfq; pop %0":"=r"(flags)); return flags&(1u<<9);
}
/* Single CPU: IRQs keep the PIT running while preemption protects the channel.
 * Never call this synchronous API from an IRQ handler or with IF cleared. */
static struct block_result operate(void *context,uint64_t lba,uint32_t count,void *buffer,unsigned int operation) {
    if(!enabled()) return (struct block_result){.error=BLOCK_CONTEXT};
    process_preempt_disable();
    if(busy) { process_preempt_enable(); return (struct block_result){.error=BLOCK_BUSY}; }
    busy=true;
    struct block_result r=operation==0 ? ata_read(context,lba,count,buffer)
        : operation==1 ? ata_write(context,lba,count,buffer) : ata_flush(context);
    busy=false; process_preempt_enable(); return r;
}
static struct block_result read_blocks(void *p,uint64_t l,uint32_t n,void *b) { return operate(p,l,n,b,0); }
static struct block_result write_blocks(void *p,uint64_t l,uint32_t n,const void *b) { return operate(p,l,n,(void *)b,1); }
static struct block_result flush_blocks(void *p) { return operate(p,0,0,0,2); }
static uint32_t pci_address(unsigned int bus,unsigned int slot,unsigned int fn,unsigned int reg) {
    return 0x80000000u|(bus<<16)|(slot<<11)|(fn<<8)|(reg&0xfc);
}
static uint32_t pci_read(unsigned int b,unsigned int s,unsigned int f,unsigned int reg) {
    uint64_t flags=irq_save(); outl(0xcf8,pci_address(b,s,f,reg)); uint32_t value=inl(0xcfc); irq_restore(flags); return value;
}
static void pci_enable_io(unsigned int b,unsigned int s,unsigned int f) {
    uint64_t flags=irq_save(); outl(0xcf8,pci_address(b,s,f,4)); uint16_t command=inw(0xcfc);
    outw(0xcfc,command|1); irq_restore(flags); /* Do not write PCI status W1C bits. */
}
static void probe_channel(uint16_t command,uint16_t control) {
    for(unsigned int i=0;i<channel_count;++i) if(channels[i].command==command) return;
    if(channel_count==4) { log_warn("ata","Limite de canais IDE atingido."); return; }
    struct ata_channel *c=&channels[channel_count++];
    *c=(struct ata_channel){.command=command,.control=control,.io={read8,read16,write8,write16,ticks,relax,0}};
    for(unsigned int slave=0;slave<2 && drive_count<BLOCK_MAX_DEVICES;++slave) {
        struct ata_drive *d=&drives[drive_count]; *d=(struct ata_drive){.channel=c,.slave=slave!=0};
        process_preempt_disable(); struct block_result r=ata_identify(d); process_preempt_enable();
        if(r.error!=BLOCK_OK) {
            if(r.error!=BLOCK_NO_DEVICE) log_warn("ata","Canal %x unidade %u: %s (status=%x erro=%x).",
                command,slave,block_error_name(r.error),r.status_reg,r.error_reg);
            continue;
        }
        struct block_device device={.name="ata0",.sectors=d->sectors,.sector_size=d->sector_size,
            .ops={read_blocks,write_blocks,flush_blocks},.context=d};
        device.name[3]='0'+drive_count; memcpy(device.model,d->model,sizeof device.model);
        if(block_register(&device)) {
            log_info("ata","%s: %s; %llu setores de %u bytes; LBA%u, flush=%s.",device.name,device.model,
                (unsigned long long)d->sectors,d->sector_size,d->lba48 ? 48u:28u,(d->flush||d->flush_ext) ? "sim":"nao");
            ++drive_count;
        }
    }
}
void storage_init(void) {
    if(initialized) return;
    initialized=true;
    for(unsigned int b=0;b<256;++b) for(unsigned int s=0;s<32;++s) {
        if((pci_read(b,s,0,0)&0xffff)==0xffff) continue;
        unsigned int functions=(pci_read(b,s,0,12)&0x800000) ? 8:1;
        for(unsigned int f=0;f<functions;++f) {
            uint32_t id=pci_read(b,s,f,0); if((id&0xffff)==0xffff) continue;
            uint32_t kind=pci_read(b,s,f,8); if((kind>>24)!=1) continue;
            unsigned int subclass=(kind>>16)&255, interface=(kind>>8)&255;
            log_info("pci","Armazenamento %u:%u.%u ID=%x classe=01/%x.",b,s,f,id,subclass);
            if(subclass!=1) { log_warn("ata","Controlador sem interface ATA PIO; ignorado."); continue; }
            pci_enable_io(b,s,f);
            for(unsigned int n=0;n<2;++n) {
                uint16_t command=n ? 0x170:0x1f0, control=n ? 0x376:0x3f6;
                if(interface&(1u<<(n*2))) {
                    uint32_t cb=pci_read(b,s,f,16+n*8), ct=pci_read(b,s,f,20+n*8);
                    if(!(cb&1) || !(ct&1) || !(cb&~3u) || !(ct&~3u) || (cb&~3u)>0xfff8 || (ct&~3u)>0xfffc) {
                        log_warn("ata","BARs IDE nativos invalidos ou MMIO; canal ignorado."); continue;
                    }
                    command=cb&~3u; control=(ct&~3u)+2;
                }
                probe_channel(command,control);
            }
        }
    }
    log_info("storage","%u dispositivo(s) de bloco disponivel(is).",(unsigned int)block_count());
}
void storage_list(void) {
    if(!block_count()) log_info("storage","Nenhum disco ATA PIO. AHCI, NVMe e VirtIO ainda nao suportados.");
    for(size_t i=0;i<block_count();++i) {
        const struct block_device *d=block_get(i);
        log_info("storage","%s: %s | %llu setores x %u bytes = %llu bytes",d->name,d->model,
            (unsigned long long)d->sectors,d->sector_size,(unsigned long long)(d->sectors*d->sector_size));
    }
}
