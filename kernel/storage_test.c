#include <ata.h>
#include <runtime.h>
#include <log.h>
#include <panic.h>

/* Destructive tests are opt-in and accept only disks created by check-storage.py. */
static const char signature[]="NEXUS-ATA-TEST-DISK-v1\n";
static uint8_t buffer[3*BLOCK_MAX_SECTOR], expected[3*BLOCK_MAX_SECTOR];
static uint8_t pattern(uint64_t lba,unsigned int offset,bool written) {
    return (uint8_t)(lba*17+offset*3+(written ? 0xa7:0x31));
}
static void require_result(struct block_result r,uint32_t count,const char *operation) {
    if(r.error!=BLOCK_OK || r.completed!=count) {
        log_error("storage-test","%s: %s, completos=%u status=%x erro=%x",operation,
            block_error_name(r.error),r.completed,r.status_reg,r.error_reg);
        PANIC(ERR_SELFTEST,"Falha no teste de armazenamento.");
    }
}
static void check(const struct block_device *d,uint64_t lba,uint32_t count,bool written,bool verify) {
    size_t bytes=(size_t)count*d->sector_size;
    for(uint32_t n=0;n<count;++n) for(unsigned int i=0;i<d->sector_size;++i)
        expected[(size_t)n*d->sector_size+i]=pattern(lba+n,i,written);
    if(written && !verify) require_result(block_write(d,lba,count,expected,bytes),count,"write");
    memset(buffer,0,sizeof buffer);
    require_result(block_read(d,lba,count,buffer,bytes),count,"read");
    if(memcmp(buffer,expected,bytes)) PANIC(ERR_SELFTEST,"Conteudo do disco diferente do esperado.");
}
void storage_selftest(bool verify) {
    if(!block_count()) PANIC(ERR_SELFTEST,"Teste de armazenamento sem disco ATA.");
    /* Check ALL signatures before the first write, including zero padding. */
    for(size_t i=0;i<block_count();++i) {
        const struct block_device *d=block_get(i);
        require_result(block_read(d,0,1,buffer,sizeof buffer),1,"signature");
        if(memcmp(buffer,signature,sizeof signature-1)) PANIC(ERR_SELFTEST,"Disco sem assinatura descartavel; escrita recusada.");
        for(size_t j=sizeof signature-1;j<d->sector_size;++j)
            if(buffer[j]) PANIC(ERR_SELFTEST,"Assinatura de teste invalida; escrita recusada.");
        if(d->sectors<32) PANIC(ERR_SELFTEST,"Disco de teste pequeno demais.");
    }
    for(size_t i=0;i<block_count();++i) {
        const struct block_device *d=block_get(i);
        check(d,2,3,false,verify);
        /* Boundary, arithmetic overflow, and buffer checks must not reach ATA. */
        memset(buffer,0x5a,sizeof buffer); memcpy(expected,buffer,sizeof buffer);
        if(block_read(d,d->sectors,1,buffer,sizeof buffer).error!=BLOCK_RANGE ||
           block_write(d,d->sectors-1,2,buffer,sizeof buffer).error!=BLOCK_RANGE ||
           block_write(d,UINT64_MAX,2,buffer,sizeof buffer).error!=BLOCK_RANGE ||
           block_read(d,2,1,buffer,d->sector_size-1).error!=BLOCK_ARGUMENT ||
           block_read(d,0,1,0,d->sector_size).error!=BLOCK_ARGUMENT ||
           block_read(d,0,BLOCK_MAX_TRANSFER+1,buffer,sizeof buffer).error!=BLOCK_ARGUMENT ||
           memcmp(buffer,expected,sizeof buffer)) PANIC(ERR_SELFTEST,"Limites de bloco violados.");
        require_result(block_read(d,d->sectors,0,0,0),0,"zero-length");
        check(d,8,3,true,verify);
        check(d,d->sectors-1,1,true,verify);
        if(d->sectors>(1ull<<28)+9) check(d,(1ull<<28)+7,2,true,verify);
        require_result(block_flush(d),0,"flush");
        log_info("storage-test","%s: leitura, escrita, flush e limites OK; setores=%llu tamanho=%u%s",d->name,
            (unsigned long long)d->sectors,d->sector_size,verify ? "; persistencia OK":"");
    }
    log_info("storage-test",verify ? "STORAGE VERIFY PASS":"STORAGE WRITE PASS");
}
void storage_error_selftest(unsigned int operation) {
    const struct block_device *d=block_get(0);
    if(!d) PANIC(ERR_SELFTEST,"Teste de erro sem disco ATA.");
    require_result(block_read(d,0,1,buffer,sizeof buffer),1,"signature");
    if(memcmp(buffer,signature,sizeof signature-1)) PANIC(ERR_SELFTEST,"Disco sem assinatura descartavel; escrita recusada.");
    for(size_t j=sizeof signature-1;j<d->sector_size;++j)
        if(buffer[j]) PANIC(ERR_SELFTEST,"Assinatura de teste invalida; escrita recusada.");
    memset(buffer,0x6d,d->sector_size);
    if(operation==3) require_result(block_write(d,8,1,buffer,sizeof buffer),1,"write before flush error");
    struct block_result r=operation==1 ? block_read(d,2,1,buffer,sizeof buffer) :
        operation==2 ? block_write(d,8,1,buffer,sizeof buffer) : block_flush(d);
    if(r.error!=BLOCK_IO || !(r.status_reg&0x21) || !r.error_reg)
        PANIC(ERR_SELFTEST,"Erro ATA injetado nao foi diagnosticado.");
    if(block_read(d,2,1,buffer,sizeof buffer).error!=BLOCK_OFFLINE || block_flush(d).error!=BLOCK_OFFLINE)
        PANIC(ERR_SELFTEST,"Canal ATA com erro nao foi isolado.");
    log_info("storage-test","STORAGE ERROR PASS: operacao=%u status=%x erro=%x completos=%u; canal isolado.",
        operation,r.status_reg,r.error_reg,r.completed);
}
