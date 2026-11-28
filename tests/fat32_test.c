#include <fat32.h>
#include <vfs_internal.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct fat32 fs;
static struct vfs v;
static struct mock { uint8_t boot[4096],info[4096]; uint32_t links[64]; uint32_t bytes,fatsize; uint64_t reads,writes,fail_sector; } mock;
static void w16(uint8_t *p,uint16_t n) { p[0]=n; p[1]=n>>8; }
static void w32(uint8_t *p,uint32_t n) { w16(p,n); w16(p+2,n>>16); }
static uint64_t data_start(void) { return 32+2*mock.fatsize; }
static void entry(uint8_t *p,const char *name,uint8_t attr,uint32_t first,uint32_t size) {
    memset(p,0,32); memcpy(p,name,11); p[11]=attr; w16(p+20,first>>16); w16(p+26,first); w32(p+28,size);
}
static struct block_result read_blocks(void *p,uint64_t lba,uint32_t count,void *out) {
    (void)p; assert(count==1 && lba>=17 && lba<70017); ++mock.reads;
    if(lba==mock.fail_sector) return (struct block_result){.error=BLOCK_IO};
    uint8_t *b=out; memset(b,0,mock.bytes); uint64_t r=lba-17;
    if(!r) memcpy(b,mock.boot,mock.bytes);
    else if(r==1) memcpy(b,mock.info,mock.bytes);
    else if(r>=32 && r<32+2*mock.fatsize) {
        uint64_t offset=(r-32)%mock.fatsize*(mock.bytes/4);
        for(unsigned int i=0;i<mock.bytes/4;++i) if(offset+i<64) w32(b+i*4,mock.links[offset+i]);
    } else if(r>=data_start()) {
        uint32_t cluster=(uint32_t)(r-data_start()+2);
        if(cluster==2) {
            for(unsigned int i=0;i<mock.bytes/32;++i) b[i*32]=0xe5;
            entry(b,"NEXUS      ",8,0,0); memset(b+32,0xff,32); b[32]=0x41; b[43]=15;
            entry(b+64,"FRAG    BIN",32,3,1300);
            entry(b+96,"SUB        ",16,5,0);
            entry(b+128,"LOOP    BIN",32,10,1024);
            entry(b+160,"BAD     BIN",32,12,1);
            entry(b+192,"FREE    BIN",32,13,1);
            entry(b+224,"RSVD    BIN",32,14,1);
            entry(b+256,"RANGE   BIN",32,15,1);
            entry(b+288,"SHORT   BIN",32,16,mock.bytes*2);
            entry(b+320,"EXTRA   BIN",32,17,1);
            entry(b+352,"EMPTY      ",32,0,0);
            entry(b+384,"LONGNA~1TXT",32,19,4);
            entry(b+416,"LOWER   TXT",32,19,4); b[428]=24;
            entry(b+448,"ANCESTOR   ",16,2,0);
        } else if(cluster==9) entry(b,"TAIL    TXT",32,19,4);
        else if(cluster==5) {
            entry(b,".          ",16,5,0); entry(b+32,"..         ",16,2,0); entry(b+64,"CHILD   TXT",32,6,7);
        } else if(cluster==6) memcpy(b,"child!\n",7);
        else if(cluster==19) memcpy(b,"tail",4);
        else for(unsigned int i=0;i<mock.bytes;++i) b[i]=(uint8_t)(cluster*17+i*3);
    }
    return (struct block_result){.completed=1};
}
static struct block_device disk;
static void setup(unsigned int bytes) {
    memset(&mock,0,sizeof mock); mock.bytes=bytes; mock.fatsize=bytes==512 ? 600:80;
    uint8_t *b=mock.boot; b[0]=0xeb; b[2]=0x90; b[510]=0x55; b[511]=0xaa;
    w16(b+11,bytes); b[13]=1; w16(b+14,32); b[16]=2; b[21]=0xf8;
    w32(b+32,70000); w32(b+36,mock.fatsize); w32(b+44,2); w16(b+48,1); w16(b+50,6);
    w32(mock.info,0x41615252); w32(mock.info+484,0x61417272); w32(mock.info+508,0xaa550000);
    w32(mock.info+488,100); w32(mock.info+492,20);
    mock.links[0]=0x0ffffff8; mock.links[1]=0x0fffffff; mock.links[2]=9; mock.links[9]=0x0fffffff;
    mock.links[3]=8; mock.links[8]=4; mock.links[4]=0xafffffff; mock.links[5]=mock.links[6]=0x0fffffff;
    mock.links[10]=11; mock.links[11]=10; mock.links[12]=0x0ffffff7; mock.links[13]=0; mock.links[14]=0x0ffffff0;
    mock.links[15]=80000; mock.links[16]=0x0fffffff; mock.links[17]=18; mock.links[18]=mock.links[19]=0x0fffffff;
    disk=(struct block_device){.sector_size=bytes,.sectors=100000,.ops={.read=read_blocks},.name="test"};
    memset(&v,0,sizeof v);
}
static void open_volume(void) { assert(fat32_open(&fs,&disk,17,70000)==FAT32_OK); assert(vfs_core_init(&v,&fs.volume)==VFS_OK); }
int main(void) {
    setup(512); open_volume(); assert(fs.geometry.clusters==68768 && fs.geometry.data_start==1232);
    assert(fs.geometry.fsinfo_valid && fs.geometry.free_known && fs.geometry.next_known && fs.geometry.free_clusters==100);
    int fd; assert(vfs_core_open(&v,1,"/frag.bin",VFS_READ,&fd)==VFS_OK);
    unsigned char buffer[1800]; struct vfs_io r=vfs_core_read(&v,1,fd,buffer,sizeof buffer); assert(!r.error && r.count==1300);
    uint32_t chain[]={3,8,4};
    for(unsigned int i=0;i<1300;++i) assert(buffer[i]==(unsigned char)(chain[i/512]*17+(i%512)*3));
    assert(!vfs_core_read(&v,1,fd,buffer,1).count); uint64_t pos;
    assert(!vfs_core_seek(&v,1,fd,500,VFS_SEEK_SET,&pos)); assert(vfs_core_read(&v,1,fd,buffer,40).count==40);
    assert(buffer[11]==(unsigned char)(3*17+511*3) && buffer[12]==(unsigned char)(8*17));
    assert(!vfs_core_close(&v,1,fd));
    assert(vfs_core_open(&v,1,"/SUB/child.txt",VFS_READ,&fd)==VFS_OK);
    assert(vfs_core_read(&v,1,fd,buffer,sizeof buffer).count==7 && !memcmp(buffer,"child!\n",7)); assert(!vfs_core_close(&v,1,fd));
    struct vfs_stat st;
    assert(!vfs_core_stat(&v,1,"/TAIL.TXT",&st) && st.size==4);
    assert(!vfs_core_stat(&v,1,"/longna~1.txt",&st)); assert(!vfs_core_stat(&v,1,"/lower.txt",&st));
    assert(!vfs_core_stat(&v,1,"/EMPTY",&st) && !st.size);
    assert(vfs_core_stat(&v,1,"/LOOP.BIN",&st)==VFS_IO && fs.error==FAT32_CYCLE);
    const char *bad[]={"BAD.BIN","FREE.BIN","RSVD.BIN","RANGE.BIN","SHORT.BIN","EXTRA.BIN"};
    for(unsigned int i=0;i<sizeof bad/sizeof *bad;++i) assert(vfs_core_stat(&v,1,bad[i],&st)==VFS_IO && fs.error==FAT32_CORRUPT);
    assert(vfs_core_stat(&v,1,"ANCESTOR",&st)==VFS_IO && fs.error==FAT32_CYCLE);
    assert(vfs_core_open(&v,1,"/TAIL.TXT",VFS_WRITE,&fd)==VFS_READ_ONLY);
    assert(vfs_core_mkdir(&v,1,"/NEW")==VFS_READ_ONLY);
    assert(!vfs_core_open(&v,1,"/",VFS_READ|VFS_OPEN_DIR,&fd)); bool end; struct vfs_dirent ent; unsigned int seen=0;
    for(;;) { assert(!vfs_core_readdir(&v,1,fd,&ent,&end)); if(end) break; ++seen; assert(strcmp(ent.name,"NEXUS")); }
    assert(seen==14); assert(!vfs_core_close(&v,1,fd));
    setup(512); mock.links[2]=2; assert(fat32_open(&fs,&disk,17,70000)==FAT32_CYCLE);
    setup(512); mock.links[9]=2; assert(fat32_open(&fs,&disk,17,70000)==FAT32_CYCLE);
    setup(512); mock.links[2]=0; assert(fat32_open(&fs,&disk,17,70000)==FAT32_CORRUPT);
    setup(512); mock.info[0]=0; assert(!fat32_open(&fs,&disk,17,70000) && !fs.geometry.fsinfo_valid);
    setup(512); w32(mock.info+488,UINT32_MAX); w32(mock.info+492,90000);
    assert(!fat32_open(&fs,&disk,17,70000) && !fs.geometry.free_known && !fs.geometry.next_known);
    setup(512); w16(mock.boot+40,0x81); assert(!fat32_open(&fs,&disk,17,70000) && fs.geometry.active_fat==1);
    setup(512); w16(mock.boot+40,0x82); assert(fat32_open(&fs,&disk,17,70000)==FAT32_GEOMETRY);
    setup(512); mock.boot[510]=0; assert(fat32_open(&fs,&disk,17,70000)==FAT32_GEOMETRY);
    setup(512); mock.boot[13]=3; assert(fat32_open(&fs,&disk,17,70000)==FAT32_GEOMETRY);
    setup(512); w32(mock.boot+32,60000); assert(fat32_open(&fs,&disk,17,70000)==FAT32_TYPE);
    setup(512); w32(mock.boot+36,1); assert(fat32_open(&fs,&disk,17,70000)==FAT32_GEOMETRY);
    setup(512); w16(mock.boot+48,32); assert(fat32_open(&fs,&disk,17,70000)==FAT32_GEOMETRY);
    setup(512); w16(mock.boot+42,1); assert(fat32_open(&fs,&disk,17,70000)==FAT32_VERSION);
    setup(512); w32(mock.boot+32,70001); assert(fat32_open(&fs,&disk,17,70000)==FAT32_GEOMETRY);
    setup(512); mock.fail_sector=18; assert(fat32_open(&fs,&disk,17,70000)==FAT32_IO);
    setup(512); assert(fat32_open(&fs,&disk,UINT64_MAX,70000)==FAT32_ARGUMENT);
    setup(512); open_volume(); assert(!vfs_core_open(&v,1,"/frag.bin",VFS_READ,&fd)); mock.fail_sector=17+data_start()+8-2;
    r=vfs_core_read(&v,1,fd,buffer,sizeof buffer); assert(r.error==VFS_IO && r.count==512);
    assert(!vfs_core_seek(&v,1,fd,0,VFS_SEEK_CUR,&pos) && pos==512);
    setup(4096); assert(!fat32_open(&fs,&disk,17,70000) && fs.geometry.sector_size==4096 && fs.geometry.cluster_size==4096);
    assert(!vfs_core_init(&v,&fs.volume)); assert(!vfs_core_open(&v,1,"/TAIL.TXT",VFS_READ,&fd));
    r=vfs_core_read(&v,1,fd,buffer,sizeof buffer); assert(!r.error && r.count==4 && !memcmp(buffer,"tail",4));
    puts("OK: FAT32 BPB/geometria/FSInfo, FAT ativa, 8.3/LFN, diretorios fragmentados, arquivos fragmentados, corrupcao e ciclos.");
}
