#include <ramfs.h>
#include <vfs_internal.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct ramfs fs;
static struct vfs v;
static unsigned char image[16387];
int main(void) {
    ramfs_init(&fs);
    for(size_t i=0;i<sizeof image;++i) image[i]=(unsigned char)(i*13+7);
    assert(ramfs_import_file(&fs,"alpha.elf",image,sizeof image)==VFS_OK);
    assert(ramfs_import_file(&fs,"empty",0,0)==VFS_OK);
    assert(ramfs_import_file(&fs,"alpha.elf",image,2)==VFS_EXISTS);
    const char *invalid[]={"",".","..","../escape","a/b","a b","a:b","a\\b"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;++i) assert(ramfs_import_file(&fs,invalid[i],image,1)==VFS_INVALID);
    assert(ramfs_import_file(&fs,"x",0,1)==VFS_INVALID);
    assert(ramfs_import_file(&fs,"x",(void *)(uintptr_t)(UINTPTR_MAX-1),3)==VFS_INVALID);
    assert(ramfs_import_file(&fs,"01234567890123456789012345678901",image,1)==VFS_NAME_LONG);
    assert(vfs_core_init(&v,&fs.volume)==VFS_OK); int fd;
    assert(vfs_core_open(&v,1,"/alpha.elf",VFS_READ|VFS_WRITE,&fd)==VFS_OK);
    unsigned char buffer[1024]; size_t offset=0;
    for(;;) {
        struct vfs_io r=vfs_core_read(&v,1,fd,buffer,sizeof buffer); assert(!r.error);
        if(!r.count) break;
        assert(!memcmp(buffer,image+offset,r.count)); offset+=r.count;
    }
    assert(offset==sizeof image);
    uint64_t position;
    assert(vfs_core_seek(&v,1,fd,-5,VFS_SEEK_END,&position)==VFS_OK);
    assert(vfs_core_read(&v,1,fd,buffer,sizeof buffer).count==5 && !memcmp(buffer,image+sizeof image-5,5));
    assert(vfs_core_write(&v,1,fd,"x",1).error==VFS_READ_ONLY);
    assert(vfs_core_close(&v,1,fd)==VFS_OK);
    assert(vfs_core_open(&v,1,"/alpha.elf",VFS_WRITE|VFS_TRUNCATE,&fd)==VFS_READ_ONLY);
    assert(image[0]==7 && fs.nodes[1].node.size==sizeof image);
    assert(vfs_core_open(&v,1,"/empty",VFS_READ,&fd)==VFS_OK);
    assert(!vfs_core_read(&v,1,fd,buffer,sizeof buffer).count);
    assert(vfs_core_close(&v,1,fd)==VFS_OK);
    fs.volume.read_only=true;
    assert(ramfs_import_file(&fs,"new",image,1)==VFS_READ_ONLY);
    assert(vfs_core_open(&v,1,"/alpha.elf",VFS_WRITE,&fd)==VFS_READ_ONLY);
    puts("OK: RAMFS importado >4KiB, leitura completa/seek/EOF, vazio, readonly, duplicatas e nomes invalidos.");
}
