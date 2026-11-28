#include <bootfs.h>
#include <vfs_internal.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct ramfs fs;
static struct vfs v;
static unsigned char data[8193];
static struct bootfs_report report;
int main(void) {
    struct limine_file files[]={
        {.address=data,.size=sizeof data,.string="alpha"},
        {.address=data,.size=1,.string="alpha"},
        {.address=data,.size=1,.string="../escape"},
        {.address=data,.size=2,.path="boot():/assets/fallback.bin"},
        {.address=data,.size=0,.string="empty"},
        {.address=0,.size=1,.string="null"},
        {.address=data,.size=BOOTFS_FILE_MAX+1ull,.string="huge"},
        {.address=data,.size=1,.string=".."},
        {.address=data,.size=1,.string="",.path="boot():/assets/"},
    };
    struct limine_file *modules[BOOTFS_SCAN_MAX];
    for(size_t i=0;i<sizeof files/sizeof *files;++i) modules[i]=&files[i];
    modules[9]=0;
    struct limine_module_response response={.module_count=10,.modules=modules};
    bootfs_populate(&fs,&response,&report); assert(report.imported==3 && report.rejected==7 && !report.truncated && fs.volume.read_only);
    assert(vfs_core_init(&v,&fs.volume)==VFS_OK); int fd;
    assert(vfs_core_open(&v,1,"/fallback.bin",VFS_READ,&fd)==VFS_OK);
    char buffer[10]; assert(vfs_core_read(&v,1,fd,buffer,sizeof buffer).count==2);
    assert(vfs_core_close(&v,1,fd)==VFS_OK);
    assert(vfs_core_open(&v,1,"/alpha",VFS_WRITE,&fd)==VFS_READ_ONLY);
    struct vfs_stat st; assert(vfs_core_stat(&v,1,"/alpha",&st)==VFS_OK && st.size==sizeof data);
    bootfs_populate(&fs,0,&report); assert(!report.imported && !report.rejected && fs.volume.read_only);
    response=(struct limine_module_response){.module_count=1,.modules=0};
    bootfs_populate(&fs,&response,&report); assert(!report.imported && report.rejected==1);
    for(unsigned int i=0;i<BOOTFS_SCAN_MAX;++i) modules[i]=&files[0];
    response=(struct limine_module_response){.module_count=BOOTFS_SCAN_MAX+100,.modules=modules};
    bootfs_populate(&fs,&response,&report); assert(report.imported==1 && report.rejected==63 && report.truncated);
    puts("OK: bootfs Limine, nomes/fallback, duplicatas, vazio/ausente, metadados invalidos e limite de inspecao.");
}
