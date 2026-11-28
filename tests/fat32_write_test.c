#include <fat32.h>
#include <vfs_internal.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { SECTORS=70000, FAT_SIZE=600, DATA=32+2*FAT_SIZE, CLUSTERS=SECTORS-DATA };
static unsigned char *image;
static struct fat32 fs;
static struct vfs v;
static unsigned int writes,flushes,fail_write,fail_flush;
static unsigned short r16(const unsigned char *p) { return p[0]|(p[1]<<8); }
static unsigned int r32(const unsigned char *p) { return r16(p)|((unsigned int)r16(p+2)<<16); }
static void w16(unsigned char *p,unsigned int n) { p[0]=n; p[1]=n>>8; }
static void w32(unsigned char *p,unsigned int n) { w16(p,n); w16(p+2,n>>16); }
static struct block_result read_blocks(void *p,uint64_t lba,uint32_t n,void *out) {
    (void)p; assert(n==1 && lba<SECTORS); memcpy(out,image+lba*512,512); return (struct block_result){.completed=1};
}
static struct block_result write_blocks(void *p,uint64_t lba,uint32_t n,const void *in) {
    (void)p; assert(n==1 && lba<SECTORS); ++writes;
    if(fail_write && writes==fail_write) return (struct block_result){.error=BLOCK_IO};
    memcpy(image+lba*512,in,512); return (struct block_result){.completed=1};
}
static struct block_result flush_blocks(void *p) {
    (void)p; ++flushes; return (struct block_result){.error=(fail_flush && flushes==fail_flush) ? BLOCK_IO:BLOCK_OK};
}
static struct block_device disk={.name="test",.sectors=SECTORS,.sector_size=512,.ops={read_blocks,write_blocks,flush_blocks}};
static bool clock_time(uint16_t *date,uint16_t *time) { *date=(46<<9)|(11<<5)|28; *time=(12<<11)|(34<<5)|28; return true; }
static void fat(unsigned int cluster,unsigned int value) {
    w32(image+32*512+cluster*4,value); w32(image+(32+FAT_SIZE)*512+cluster*4,value);
}
static unsigned int get(unsigned int cluster) { return r32(image+32*512+cluster*4)&0xfffffff; }
static void setup(void) {
    memset(image,0,SECTORS*512); memset(&v,0,sizeof v); writes=flushes=fail_write=fail_flush=0;
    unsigned char *b=image; b[0]=0xeb; b[2]=0x90; b[510]=0x55; b[511]=0xaa;
    w16(b+11,512); b[13]=1; w16(b+14,32); b[16]=2; b[21]=0xf8;
    w32(b+32,SECTORS); w32(b+36,FAT_SIZE); w32(b+44,2); w16(b+48,1); w16(b+50,6);
    memcpy(image+6*512,b,512);
    w32(image+512,0x41615252); w32(image+512+484,0x61417272); w32(image+512+508,0xaa550000);
    w32(image+512+488,CLUSTERS-1); w32(image+512+492,3); memcpy(image+7*512,image+512,512);
    fat(0,0xffffff8); fat(1,0xfffffff); fat(2,0xfffffff);
    assert(!fat32_open(&fs,&disk,0,SECTORS));
}
static void mount_rw(void) { assert(!fat32_enable_write(&fs,clock_time)); assert(!vfs_core_init(&v,&fs.volume)); }
static int open_file(const char *name,unsigned int flags) {
    int fd; assert(!vfs_core_open(&v,1,name,flags,&fd)); return fd;
}
static unsigned int check_free(void) {
    unsigned int free_count=0;
    for(unsigned int c=2;c<CLUSTERS+2;++c) if(!get(c)) ++free_count;
    assert(!memcmp(image+32*512,image+(32+FAT_SIZE)*512,FAT_SIZE*512));
    assert(r32(image+512+488)==free_count && r32(image+7*512+488)==free_count);
    assert(fs.geometry.free_clusters==free_count); return free_count;
}
int main(void) {
    image=malloc(SECTORS*512); assert(image); setup(); mount_rw();
    unsigned int initial=fs.geometry.free_clusters;
    int fd=open_file("/hello.txt",VFS_CREATE|VFS_READ|VFS_WRITE);
    unsigned char input[1600],output[4096]; for(unsigned int i=0;i<sizeof input;++i) input[i]=(unsigned char)(i*7+11);
    struct vfs_io io=vfs_core_write(&v,1,fd,input,sizeof input); assert(!io.error && io.count==sizeof input);
    assert(check_free()==initial-4 && !(get(1)&(1u<<27)));
    unsigned char *record=image+DATA*512; assert(!memcmp(record,"HELLO   TXT",11));
    assert(r32(record+28)==1600 && r16(record+24)==((46<<9)|(11<<5)|28) && r16(record+22)==((12<<11)|(34<<5)|28));
    uint64_t position; assert(!vfs_core_seek(&v,1,fd,0,VFS_SEEK_SET,&position));
    io=vfs_core_read(&v,1,fd,output,sizeof output); assert(!io.error && io.count==1600 && !memcmp(output,input,1600));
    assert(!vfs_core_truncate(&v,1,"/hello.txt",600)); assert(check_free()==initial-2);
    assert(!vfs_core_truncate(&v,1,"/hello.txt",2000)); assert(check_free()==initial-4);
    assert(!vfs_core_seek(&v,1,fd,600,VFS_SEEK_SET,&position)); io=vfs_core_read(&v,1,fd,output,sizeof output);
    assert(!io.error && io.count==1400); for(unsigned int i=0;i<1400;++i) assert(!output[i]);
    assert(!vfs_core_close(&v,1,fd));
    fd=open_file("/hello.txt",VFS_WRITE|VFS_APPEND); io=vfs_core_write(&v,1,fd,"tail",4); assert(!io.error && io.count==4); assert(!vfs_core_close(&v,1,fd));
    assert(!vfs_core_mkdir(&v,1,"/a")); assert(!vfs_core_mkdir(&v,1,"/b")); assert(!vfs_core_mkdir(&v,1,"/a/sub"));
    assert(!vfs_core_rename(&v,1,"/hello.txt","/a/moved.bin"));
    assert(!vfs_core_rename(&v,1,"/a/sub","/b/sub"));
    struct vfs_node *dir; assert(!fs.volume.ops->lookup(fs.volume.root,"B",&dir));
    struct vfs_node *sub; assert(!fs.volume.ops->lookup(dir,"SUB",&sub));
    struct fat32_node *dn=(struct fat32_node *)dir,*sn=(struct fat32_node *)sub;
    unsigned char *dotdot=image+(DATA+(sn->first-2))*512+32;
    assert(r16(dotdot+26)==dn->first && !memcmp(dotdot,"..         ",11));
    assert(vfs_core_remove(&v,1,"/a",true)==VFS_NOT_EMPTY);
    assert(!vfs_core_remove(&v,1,"/a/moved.bin",false));
    assert(!vfs_core_remove(&v,1,"/a",true)); assert(!vfs_core_remove(&v,1,"/b/sub",true)); assert(!vfs_core_remove(&v,1,"/b",true));
    assert(check_free()==initial);
    /* Reuse node and directory slots beyond the pool's lifetime capacity. */
    for(unsigned int i=0;i<140;++i) { fd=open_file("/reuse",VFS_CREATE|VFS_WRITE); assert(!vfs_core_close(&v,1,fd)); assert(!vfs_core_remove(&v,1,"/reuse",false)); }
    assert(!vfs_core_sync_all(&v) && (get(1)&(1u<<27)));
    /* Directory growth allocates and preserves all preexisting records. */
    for(unsigned int i=0;i<22;++i) { char name[13]; snprintf(name,sizeof name,"/F%u",i); fd=open_file(name,VFS_CREATE|VFS_WRITE); assert(!vfs_core_close(&v,1,fd)); }
    assert(fs.nodes[0].chain_length>=2 && check_free()==initial-1);
    assert(!vfs_core_sync_all(&v));
    struct fat32 reopened; memset(&v,0,sizeof v); assert(!fat32_open(&reopened,&disk,0,SECTORS)); assert(!vfs_core_init(&v,&reopened.volume));
    struct vfs_stat st; assert(!vfs_core_stat(&v,1,"/F21",&st));
    /* A completely full FAT still permits zero-length files, but no growth. */
    setup(); for(unsigned int c=3;c<CLUSTERS+2;++c) fat(c,0xfffffff); mount_rw();
    fd=open_file("/full",VFS_CREATE|VFS_WRITE); unsigned int before_writes=writes;
    io=vfs_core_write(&v,1,fd,input,1); assert(io.error==VFS_NO_SPACE && !io.count && writes==before_writes);
    assert(vfs_core_mkdir(&v,1,"/no")==VFS_NO_SPACE && writes==before_writes);
    /* Stale FSInfo must never cause an allocated cluster to be reused. */
    setup(); fat(3,0xfffffff); w32(image+512+488,CLUSTERS); mount_rw(); assert(fs.geometry.free_clusters==CLUSTERS-2);
    fd=open_file("/fresh",VFS_CREATE|VFS_WRITE); assert(!vfs_core_write(&v,1,fd,"x",1).error); assert(get(3)==0xfffffff && get(4)==0xfffffff);
    /* FAT high nibble preserved independently in both mirrored copies. */
    setup(); w32(image+32*512+3*4,0xa0000000); w32(image+(32+FAT_SIZE)*512+3*4,0xb0000000); mount_rw();
    fd=open_file("/nibble",VFS_CREATE|VFS_WRITE); assert(!vfs_core_write(&v,1,fd,"x",1).error);
    assert((r32(image+32*512+12)>>28)==10 && (r32(image+(32+FAT_SIZE)*512+12)>>28)==11);
    /* Failed writes/flush quarantine the mount and cannot report success. */
    setup(); mount_rw(); fd=open_file("/fail",VFS_CREATE|VFS_WRITE); fail_write=writes+1;
    io=vfs_core_write(&v,1,fd,input,3); assert(io.error==VFS_IO && !io.count && fs.poisoned && fs.volume.read_only);
    assert(vfs_core_sync_all(&v)==VFS_IO && !(get(1)&(1u<<27)));
    setup(); mount_rw(); fd=open_file("/fail",VFS_CREATE|VFS_WRITE); fail_flush=flushes+1;
    assert(vfs_core_write(&v,1,fd,input,3).error==VFS_IO && fs.poisoned);
    setup(); fat(1,0x07ffffff); assert(!fat32_open(&fs,&disk,0,SECTORS)); assert(fat32_enable_write(&fs,clock_time)==FAT32_DIRTY);
    setup(); w32(image+(32+FAT_SIZE)*512+12,0xfffffff); assert(fat32_enable_write(&fs,clock_time)==FAT32_DIRTY);
    setup(); for(unsigned int c=3;c<CLUSTERS+1;++c) fat(c,0xfffffff); mount_rw();
    fd=open_file("/onefree",VFS_CREATE|VFS_WRITE|VFS_APPEND); before_writes=writes;
    io=vfs_core_write(&v,1,fd,input,1024); assert(io.error==VFS_NO_SPACE && !io.count && writes==before_writes);
    io=vfs_core_write(&v,1,fd,input,512); assert(!io.error && io.count==512 && !fs.geometry.free_clusters);
    before_writes=writes; io=vfs_core_write(&v,1,fd,input,1); assert(io.error==VFS_NO_SPACE && !io.count && writes==before_writes);
    assert(!vfs_core_truncate(&v,1,"/onefree",0) && fs.geometry.free_clusters==1);
    for(unsigned int point=1;point<=12;++point) {
        setup(); mount_rw(); fd=open_file("/inject",VFS_CREATE|VFS_WRITE); fail_write=writes+point;
        io=vfs_core_write(&v,1,fd,input,900);
        if(io.error) assert(io.error==VFS_IO && fs.poisoned && fs.volume.read_only && !(get(1)&(1u<<27)));
        else assert(io.count==900 && !fs.poisoned);
    }
    /* A valid LFN set is kept during content updates and removed with its SFN. */
    setup(); unsigned char *lfn=image+DATA*512,*sfn=lfn+32;
    memcpy(sfn,"LONGNA~1TXT",11); sfn[11]=32;
    unsigned char sum=0; for(unsigned int i=0;i<11;++i) sum=(unsigned char)((sum>>1)|(sum<<7))+sfn[i];
    memset(lfn,0xff,32); lfn[0]=0x41; lfn[11]=15; lfn[12]=0; lfn[13]=sum; w16(lfn+26,0);
    const unsigned int positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    const char *long_name="conflict.txt"; for(unsigned int i=0;i<13;++i) w16(lfn+positions[i],i<12 ? (unsigned char)long_name[i]:0);
    assert(!fat32_open(&fs,&disk,0,SECTORS)); mount_rw();
    unsigned char saved_lfn[32]; memcpy(saved_lfn,lfn,32);
    assert(vfs_core_open(&v,1,"/CONFLICT.TXT",VFS_CREATE|VFS_WRITE,&fd)==VFS_EXISTS);
    fd=open_file("/LONGNA~1.TXT",VFS_WRITE); assert(!vfs_core_write(&v,1,fd,"preserved",9).error);
    assert(!memcmp(saved_lfn,lfn,32)); assert(!vfs_core_close(&v,1,fd));
    assert(!vfs_core_rename(&v,1,"/LONGNA~1.TXT","/SHORT.TXT")); assert(lfn[0]==0xe5 && !memcmp(sfn,"SHORT   TXT",11));
    assert(!vfs_core_remove(&v,1,"/SHORT.TXT",false)); assert(sfn[0]==0xe5);
    free(image); puts("OK: FAT32 escrita, FATs/FSInfo, create/mkdir, crescimento/append/truncate, move/delete, disco cheio, sync e falhas.");
}
