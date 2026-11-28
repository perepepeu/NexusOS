#include <vfs_internal.h>
#include <ramfs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct vfs v;
static struct ramfs root,a,b,extra[VFS_MOUNTS];
static enum vfs_error sync_fail(struct vfs_volume *p) { (void)p; return VFS_IO; }
static void reset(void) {
    memset(&v,0,sizeof v); ramfs_init(&root); ramfs_init(&a); ramfs_init(&b);
    assert(vfs_core_init(&v,&root.volume)==VFS_OK);
}
static void ok(enum vfs_error e) { assert(e==VFS_OK); }
static int open_file(uint64_t pid,const char *path,unsigned int flags) {
    int fd=-1; ok(vfs_core_open(&v,pid,path,flags,&fd)); assert(fd>=3); return fd;
}
static void cwd(uint64_t pid,const char *path) {
    char buffer[256]; ok(vfs_core_getcwd(&v,pid,buffer,sizeof buffer)); assert(!strcmp(buffer,path));
}
int main(void) {
    reset(); assert(vfs_core_init(&v,&root.volume)==VFS_INVALID);
    ok(vfs_core_mkdir(&v,1,"/home")); ok(vfs_core_mkdir(&v,1,"/home/user/"));
    ok(vfs_core_chdir(&v,1,"//home///user/./")); cwd(1,"/home/user"); cwd(2,"/");
    int fd=open_file(1,"./hello",VFS_CREATE|VFS_READ|VFS_WRITE);
    struct vfs_io r=vfs_core_write(&v,1,fd,"abcdef",6); assert(!r.error && r.count==6);
    assert(vfs_core_read(&v,2,fd,(char[8]){0},8).error==VFS_BAD_FD);
    int independent=open_file(2,"/home/user/hello",VFS_READ);
    char buffer[8192]={0}; r=vfs_core_read(&v,2,independent,buffer,3); assert(!r.error && r.count==3 && !memcmp(buffer,"abc",3));
    uint64_t position=999; ok(vfs_core_seek(&v,1,fd,-3,VFS_SEEK_CUR,&position)); assert(position==3);
    r=vfs_core_write(&v,1,fd,"XY",2); assert(!r.error && r.count==2);
    ok(vfs_core_seek(&v,1,fd,0,VFS_SEEK_SET,&position)); r=vfs_core_read(&v,1,fd,buffer,8);
    assert(!r.error && r.count==6 && !memcmp(buffer,"abcXYf",6));
    assert(vfs_core_read(&v,1,fd,buffer,8).count==0);
    assert(vfs_core_seek(&v,1,fd,-1,VFS_SEEK_SET,&position)==VFS_INVALID);
    assert(vfs_core_seek(&v,1,fd,INT64_MIN,VFS_SEEK_CUR,&position)==VFS_INVALID);
    ok(vfs_core_seek(&v,1,fd,INT64_MAX,VFS_SEEK_SET,&position));
    assert(vfs_core_seek(&v,1,fd,1,VFS_SEEK_CUR,&position)==VFS_INVALID);
    assert(vfs_core_write(&v,1,fd,"a",1).error==VFS_INVALID);
    ok(vfs_core_seek(&v,1,fd,10,VFS_SEEK_SET,&position)); assert(vfs_core_write(&v,1,fd,"Z",1).count==1);
    ok(vfs_core_seek(&v,1,fd,6,VFS_SEEK_SET,&position)); r=vfs_core_read(&v,1,fd,buffer,5);
    assert(r.count==5 && !memcmp(buffer,"\0\0\0\0Z",5));
    assert(vfs_core_write(&v,2,independent,"x",1).error==VFS_ACCESS);
    assert(vfs_core_read(&v,1,fd,0,1).error==VFS_INVALID);
    assert(vfs_core_read(&v,1,fd,(void *)(uintptr_t)(UINTPTR_MAX-1),3).error==VFS_INVALID);
    assert(vfs_core_write(&v,1,fd,buffer,VFS_IO_MAX+1).error==VFS_INVALID);
    assert(!vfs_core_write(&v,1,fd,0,0).error);
    struct vfs_stat st;
    assert(vfs_core_stat(&v,1,"hello/..",&st)==VFS_NOT_DIR);
    assert(vfs_core_stat(&v,1,"hello/.",&st)==VFS_NOT_DIR);
    assert(vfs_core_stat(&v,1,"hello/",&st)==VFS_NOT_DIR);
    assert(vfs_core_stat(&v,1,"",&st)==VFS_INVALID);
    assert(vfs_core_mkdir(&v,1,"missing/child")==VFS_NOT_FOUND);
    assert(vfs_core_mkdir(&v,1,"hello/sub")==VFS_NOT_DIR);
    int temporary;
    assert(vfs_core_open(&v,1,"hello",VFS_READ|VFS_CREATE|VFS_EXCLUSIVE,&temporary)==VFS_EXISTS);
    assert(vfs_core_open(&v,1,"hello",VFS_TRUNCATE|VFS_READ,&temporary)==VFS_INVALID);
    assert(vfs_core_open(&v,1,"/",VFS_READ,&temporary)==VFS_IS_DIR);
    assert(vfs_core_open(&v,1,"hello",VFS_READ|VFS_OPEN_DIR,&temporary)==VFS_NOT_DIR);
    assert(vfs_core_open(&v,1,"new/",VFS_READ|VFS_CREATE,&temporary)==VFS_NOT_DIR);
    char longname[33]; memset(longname,'n',32); longname[32]=0;
    assert(vfs_core_mkdir(&v,1,longname)==VFS_NAME_LONG);
    char longpath[VFS_PATH_MAX+1]; memset(longpath,'/',VFS_PATH_MAX); longpath[VFS_PATH_MAX]=0;
    assert(vfs_core_stat(&v,1,longpath,&st)==VFS_NAME_LONG);
    ok(vfs_core_close(&v,1,fd)); assert(vfs_core_close(&v,1,fd)==VFS_BAD_FD);
    fd=open_file(1,"hello",VFS_WRITE|VFS_TRUNCATE); ok(vfs_core_stat(&v,1,"hello",&st)); assert(!st.size);
    assert(vfs_core_read(&v,1,fd,buffer,1).error==VFS_ACCESS); ok(vfs_core_close(&v,1,fd));
    fd=open_file(1,"hello",VFS_WRITE|VFS_APPEND); int append=open_file(2,"/home/user/hello",VFS_WRITE|VFS_APPEND);
    assert(vfs_core_write(&v,1,fd,"AA",2).count==2); assert(vfs_core_write(&v,2,append,"BB",2).count==2);
    ok(vfs_core_seek(&v,1,fd,0,VFS_SEEK_SET,&position)); assert(vfs_core_write(&v,1,fd,"CC",2).count==2);
    ok(vfs_core_stat(&v,1,"hello",&st)); assert(st.size==6);
    ok(vfs_core_seek(&v,2,independent,0,VFS_SEEK_SET,&position)); assert(vfs_core_read(&v,2,independent,buffer,8).count==6 && !memcmp(buffer,"AABBCC",6));
    int dir=open_file(1,".",VFS_READ|VFS_OPEN_DIR); struct vfs_dirent entry; bool end;
    ok(vfs_core_readdir(&v,1,dir,&entry,&end)); assert(!end && !strcmp(entry.name,"hello") && entry.size==6);
    ok(vfs_core_readdir(&v,1,dir,&entry,&end)); assert(end);
    ok(vfs_core_seek(&v,1,dir,0,VFS_SEEK_SET,&position)); ok(vfs_core_readdir(&v,1,dir,&entry,&end)); assert(!end);
    assert(vfs_core_read(&v,1,dir,buffer,1).error==VFS_IS_DIR);
    ok(vfs_core_mkdir(&v,1,"/mnt")); ok(vfs_core_mount(&v,1,"/mnt",&a.volume));
    assert(vfs_core_mount(&v,1,"/mnt",&b.volume)==VFS_BUSY);
    assert(vfs_core_mount(&v,1,"/home",&a.volume)==VFS_BUSY);
    ok(vfs_core_mkdir(&v,1,"/mnt/sub")); ok(vfs_core_mount(&v,1,"/mnt/sub",&b.volume));
    assert(vfs_core_unmount(&v,1,"/mnt")==VFS_BUSY);
    ok(vfs_core_chdir(&v,2,"/mnt/sub")); cwd(2,"/mnt/sub");
    assert(vfs_core_unmount(&v,1,"/mnt/sub")==VFS_BUSY);
    ok(vfs_core_chdir(&v,2,"..")); cwd(2,"/mnt");
    int mounted=open_file(1,"/mnt/sub/kept",VFS_READ|VFS_WRITE|VFS_CREATE);
    assert(vfs_core_unmount(&v,1,"/mnt/sub")==VFS_BUSY); ok(vfs_core_close(&v,1,mounted));
    struct vfs_ops failing=*b.volume.ops; failing.sync=sync_fail; const struct vfs_ops *saved=b.volume.ops; b.volume.ops=&failing;
    assert(vfs_core_unmount(&v,1,"/mnt/sub")==VFS_IO);
    ok(vfs_core_stat(&v,1,"/mnt/sub/kept",&st)); b.volume.ops=saved;
    ok(vfs_core_unmount(&v,1,"/mnt/sub")); assert(vfs_core_stat(&v,1,"/mnt/sub/kept",&st)==VFS_NOT_FOUND);
    assert(vfs_core_unmount(&v,1,"/mnt")==VFS_BUSY); vfs_core_reap(&v,2);
    ok(vfs_core_unmount(&v,1,"/mnt")); ok(vfs_core_mount(&v,1,"/mnt",&b.volume));
    ok(vfs_core_stat(&v,1,"/mnt/kept",&st));
    b.volume.read_only=true;
    assert(vfs_core_mkdir(&v,1,"/mnt/no")==VFS_READ_ONLY);
    assert(vfs_core_open(&v,1,"/mnt/kept",VFS_WRITE,&temporary)==VFS_READ_ONLY);
    temporary=open_file(1,"/mnt/kept",VFS_READ); ok(vfs_core_close(&v,1,temporary));
    ok(vfs_core_unmount(&v,1,"/mnt"));
    ok(vfs_core_chdir(&v,1,"../../../../..")); cwd(1,"/");
    assert(vfs_core_unmount(&v,1,"/")==VFS_INVALID);
    vfs_core_reap(&v,1); struct vfs_stats stats; vfs_core_stats(&v,&stats);
    assert(stats.mounts==1 && !stats.sessions && !stats.descriptors);
    /* Full descriptor table rejects before create/truncate side effects. */
    reset(); fd=open_file(1,"/f",VFS_READ|VFS_WRITE|VFS_CREATE);
    for(int i=VFS_FIRST_FD+1;i<VFS_FDS;++i) (void)open_file(1,"/f",VFS_READ);
    assert(vfs_core_open(&v,1,"/new",VFS_READ|VFS_CREATE,&temporary)==VFS_NO_SPACE);
    assert(vfs_core_stat(&v,2,"/new",&st)==VFS_NOT_FOUND);
    /* Partial writes update offset even on ENOSPC. */
    memset(buffer,'q',sizeof buffer); r=vfs_core_write(&v,1,fd,buffer,sizeof buffer);
    assert(r.error==VFS_NO_SPACE && r.count==RAMFS_FILE_SIZE);
    ok(vfs_core_seek(&v,1,fd,0,VFS_SEEK_CUR,&position)); assert(position==RAMFS_FILE_SIZE);
    assert(vfs_core_write(&v,1,fd,"x",1).error==VFS_NO_SPACE);
    vfs_core_reap(&v,1); vfs_core_reap(&v,2);
    for(unsigned int i=1;i<=VFS_PROCESSES;++i) cwd(i,"/");
    assert(vfs_core_getcwd(&v,VFS_PROCESSES+1,buffer,sizeof buffer)==VFS_NO_SPACE);
    for(unsigned int i=1;i<=VFS_PROCESSES;++i) vfs_core_reap(&v,i);
    reset();
    for(unsigned int i=1;i<VFS_MOUNTS;++i) {
        char name[16]; snprintf(name,sizeof name,"/m%u",i); ramfs_init(&extra[i]);
        ok(vfs_core_mkdir(&v,1,name)); ok(vfs_core_mount(&v,1,name,&extra[i].volume));
    }
    ok(vfs_core_mkdir(&v,1,"/full")); assert(vfs_core_mount(&v,1,"/full",&a.volume)==VFS_NO_SPACE);
    puts("OK: VFS caminhos, ./.., mounts aninhados, unmount seguro, FDs/PID, offsets, append, limites e reap.");
}
