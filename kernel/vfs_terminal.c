#include <vfs.h>
#include <ramfs.h>
#include <runtime.h>
#include <console.h>
#include <serial.h>
#include <io.h>
#include <log.h>

static struct ramfs demo;
static bool demo_ready;
static void put(const char *p) { uint64_t f=irq_save(); console_write(p); serial_write(p); irq_restore(f); }
static void error(enum vfs_error e) { if(e) { put("VFS: "); put(vfs_error_name(e)); put(".\n"); } }
static bool single(const char *s) { while(*s) { if(*s==' ' || *s=='\t') return false; ++s; } return true; }
bool vfs_terminal_command(const char *name,const char *args) {
    enum vfs_error e=VFS_OK;
    if(!strcmp(name,"pwd")) {
        if(*args) { put("Uso: pwd\n"); return true; }
        char path[VFS_PATH_MAX]; e=vfs_getcwd(path,sizeof path); if(!e) { put(path); put("\n"); }
    } else if(!strcmp(name,"cd")) {
        if(!single(args)) { put("Uso: cd [CAMINHO]\n"); return true; }
        e=vfs_chdir(*args ? args:"/");
    } else if(!strcmp(name,"mkdir")) {
        if(!*args || !single(args)) { put("Uso: mkdir CAMINHO\n"); return true; }
        e=vfs_mkdir(args);
    } else if(!strcmp(name,"ls")) {
        if(!single(args)) { put("Uso: ls [CAMINHO]\n"); return true; }
        int fd; e=vfs_open(*args ? args:".",VFS_READ|VFS_OPEN_DIR,&fd);
        if(!e) {
            struct vfs_dirent entry; bool end=false;
            while(!(e=vfs_readdir(fd,&entry,&end)) && !end) {
                put(entry.name); put(entry.type==VFS_DIRECTORY ? "/\n":"\n");
            }
            enum vfs_error closed=vfs_close(fd); if(!e) e=closed;
        }
    } else if(!strcmp(name,"cat")) {
        if(!*args || !single(args)) { put("Uso: cat CAMINHO\n"); return true; }
        int fd; e=vfs_open(args,VFS_READ,&fd);
        if(!e) {
            char data[129];
            for(;;) {
                struct vfs_io r=vfs_read(fd,data,sizeof data-1); e=r.error;
                if(r.count) {
                    uint64_t flags=irq_save(); console_write_n(data,r.count);
                    serial_write_n(data,r.count);
                    irq_restore(flags);
                }
                if(e || !r.count) break;
            }
            enum vfs_error closed=vfs_close(fd); if(!e) e=closed;
        }
    } else if(!strcmp(name,"write") || !strcmp(name,"append")) {
        size_t n=0; while(args[n] && args[n]!=' ' && args[n]!='\t') ++n;
        if(!n || n>=VFS_PATH_MAX || !args[n]) { put("Uso: write|append CAMINHO TEXTO\n"); return true; }
        char path[VFS_PATH_MAX]; memcpy(path,args,n); path[n]=0;
        const char *text=args+n; while(*text==' ' || *text=='\t') ++text;
        char contents[128]; size_t bytes=strlen(text);
        if(bytes+1>sizeof contents) { put("Texto grande demais.\n"); return true; }
        memcpy(contents,text,bytes); contents[bytes++]='\n';
        int fd; e=vfs_open(path,VFS_WRITE|VFS_CREATE|(!strcmp(name,"append") ? VFS_APPEND:VFS_TRUNCATE),&fd);
        if(!e) {
            struct vfs_io r=vfs_write(fd,contents,bytes); e=r.error;
            if(!e && r.count!=bytes) e=VFS_IO;
            enum vfs_error closed=vfs_close(fd); if(!e) e=closed;
        }
    } else if(!strcmp(name,"mount")) {
        if(!*args || !single(args)) { put("Uso: mount DIRETORIO (volume RAMFS de demonstracao)\n"); return true; }
        if(!demo_ready) { ramfs_init(&demo); demo_ready=true; }
        e=vfs_mount(args,&demo.volume); if(!e) put("Volume RAMFS montado.\n");
    } else if(!strcmp(name,"umount")) {
        if(!*args || !single(args)) { put("Uso: umount DIRETORIO\n"); return true; }
        e=vfs_unmount(args); if(!e) put("Volume desmontado com seguranca.\n");
    } else if(!strcmp(name,"rm") || !strcmp(name,"rmdir")) {
        if(!*args || !single(args)) { put("Uso: rm|rmdir CAMINHO\n"); return true; }
        e=vfs_remove(args,!strcmp(name,"rmdir"));
    } else if(!strcmp(name,"mv") || !strcmp(name,"truncate")) {
        char buffer[VFS_PATH_MAX]; size_t bytes=strlen(args);
        if(!bytes || bytes>=sizeof buffer) { put("Uso: mv ORIGEM DESTINO | truncate CAMINHO [BYTES]\n"); return true; }
        memcpy(buffer,args,bytes+1); char *second=buffer;
        while(*second && *second!=' ' && *second!='\t') ++second;
        if(*second) *second++=0;
        while(*second==' ' || *second=='\t') ++second;
        if(!single(second)) { put("Argumentos demais.\n"); return true; }
        if(!strcmp(name,"mv")) {
            if(!*second) { put("Uso: mv ORIGEM DESTINO\n"); return true; }
            e=vfs_rename(buffer,second);
        } else {
            uint64_t size=0;
            for(char *p=second;*p;++p) {
                if(*p<'0' || *p>'9' || size>(UINT64_MAX-(unsigned int)(*p-'0'))/10) { put("Tamanho invalido.\n"); return true; }
                size=size*10+(unsigned int)(*p-'0');
            }
            e=vfs_truncate(buffer,size);
        }
    } else if(!strcmp(name,"sync")) {
        if(!single(args)) { put("Uso: sync [CAMINHO]\n"); return true; }
        e=*args ? vfs_sync(args):vfs_sync_all(); if(!e) put("Dados e metadados sincronizados.\n");
    } else if(!strcmp(name,"vfsinfo")) {
        if(*args) { put("Uso: vfsinfo\n"); return true; }
        struct vfs_stats s; vfs_get_stats(&s);
        log_info("vfs","VFS: volumes=%u processos=%u descritores=%u",s.mounts,s.sessions,s.descriptors);
    } else return false;
    error(e); return true;
}
