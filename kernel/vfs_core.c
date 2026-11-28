#include <vfs_internal.h>
#include <runtime.h>
#include <block.h>

static bool valid_volume(const struct vfs_volume *v) {
    return v && v->ops && v->ops->lookup && v->root && v->root->volume==v &&
        v->root->type==VFS_DIRECTORY && v->root->parent==v->root;
}
enum vfs_error vfs_core_init(struct vfs *v,struct vfs_volume *root) {
    if(!v || v->ready || !valid_volume(root)) return VFS_INVALID;
    *v=(struct vfs){0}; v->ready=true;
    v->mounts[0]=(struct vfs_mountpoint){.used=true,.volume=root}; return VFS_OK;
}
static struct vfs_session *session(struct vfs *v,uint64_t pid,bool create) {
    if(!v || !v->ready || !pid) return 0;
    struct vfs_session *free=0;
    for(unsigned int i=0;i<VFS_PROCESSES;++i) {
        if(v->sessions[i].pid==pid) return &v->sessions[i];
        if(!v->sessions[i].pid && !free) free=&v->sessions[i];
    }
    if(!create || !free) return 0;
    *free=(struct vfs_session){.pid=pid,.cwd={0,v->mounts[0].volume->root}};
    ++v->mounts[0].users; return free;
}
static enum vfs_error path_length(const char *p,size_t *n) {
    if(!p || !*p) return VFS_INVALID;
    *n=0; while(*n<VFS_PATH_MAX && p[*n]) ++*n;
    return *n==VFS_PATH_MAX ? VFS_NAME_LONG:VFS_OK;
}
static bool same(struct vfs_location a,struct vfs_location b) { return a.mount==b.mount && a.node==b.node; }
static void enter_mount(struct vfs *v,struct vfs_location *p) {
    for(unsigned int i=1;i<VFS_MOUNTS;++i) if(v->mounts[i].used && same(v->mounts[i].covered,*p)) {
        *p=(struct vfs_location){i,v->mounts[i].volume->root}; return;
    }
}
static enum vfs_error resolve(struct vfs *v,struct vfs_session *s,const char *path,struct vfs_location *out) {
    size_t length; enum vfs_error e=path_length(path,&length); if(e) return e;
    struct vfs_location p=path[0]=='/' ? (struct vfs_location){0,v->mounts[0].volume->root}:s->cwd;
    size_t i=0;
    while(i<length) {
        while(path[i]=='/') ++i;
        if(!path[i]) break;
        if(p.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
        char component[VFS_NAME_MAX+1]; size_t n=0;
        while(path[i] && path[i]!='/') {
            if(n==VFS_NAME_MAX) return VFS_NAME_LONG;
            component[n++]=path[i++];
        }
        component[n]=0;
        if(!strcmp(component,".")) continue;
        if(!strcmp(component,"..")) {
            if(p.node==v->mounts[p.mount].volume->root) {
                if(p.mount) { p=v->mounts[p.mount].covered; p.node=p.node->parent; }
            } else p.node=p.node->parent;
            continue;
        }
        struct vfs_node *next=0;
        e=v->mounts[p.mount].volume->ops->lookup(p.node,component,&next);
        if(e) return e;
        if(!next || next->volume!=v->mounts[p.mount].volume || next->parent!=p.node) return VFS_IO;
        p.node=next; enter_mount(v,&p);
    }
    if(path[length-1]=='/' && p.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
    *out=p; return VFS_OK;
}
static enum vfs_error parent_path(struct vfs *v,struct vfs_session *s,const char *path,struct vfs_location *out,char leaf[VFS_NAME_MAX+1]) {
    size_t n; enum vfs_error e=path_length(path,&n); if(e) return e;
    char copy[VFS_PATH_MAX]; memcpy(copy,path,n+1);
    while(n>1 && copy[n-1]=='/') copy[--n]=0;
    size_t start=n; while(start && copy[start-1]!='/') --start;
    const char *name=copy+start; size_t namesize=n-start;
    if(!namesize) return VFS_EXISTS;
    if(namesize>VFS_NAME_MAX) return VFS_NAME_LONG;
    memcpy(leaf,name,namesize+1);
    struct vfs_location parent=s->cwd;
    if(start) {
        copy[start==1 ? 1:start-1]=0;
        e=resolve(v,s,copy,&parent); if(e) return e;
    }
    if(parent.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
    if(!strcmp(leaf,".") || !strcmp(leaf,"..")) return VFS_EXISTS;
    *out=parent; return VFS_OK;
}
static enum vfs_error create_node(struct vfs *v,struct vfs_session *s,const char *path,enum vfs_type type,struct vfs_location *out) {
    size_t n; enum vfs_error e=path_length(path,&n); if(e) return e;
    if(type==VFS_FILE && path[n-1]=='/') return VFS_NOT_DIR;
    struct vfs_location parent; char leaf[VFS_NAME_MAX+1];
    e=parent_path(v,s,path,&parent,leaf); if(e) return e;
    struct vfs_volume *volume=v->mounts[parent.mount].volume;
    if(volume->read_only) return VFS_READ_ONLY;
    if(!volume->ops->create) return VFS_UNSUPPORTED;
    struct vfs_node *node=0;
    e=volume->ops->create(parent.node,leaf,type,&node); if(e) return e;
    if(!node || node->volume!=volume || node->parent!=parent.node || node->type!=type) return VFS_IO;
    *out=(struct vfs_location){parent.mount,node}; return VFS_OK;
}
enum vfs_error vfs_core_open(struct vfs *v,uint64_t pid,const char *path,unsigned int flags,int *fd) {
    const unsigned int mask=VFS_READ|VFS_WRITE|VFS_CREATE|VFS_EXCLUSIVE|VFS_TRUNCATE|VFS_APPEND|VFS_OPEN_DIR;
    if(!fd || flags&~mask || !(flags&(VFS_READ|VFS_WRITE)) ||
        ((flags&(VFS_TRUNCATE|VFS_APPEND)) && !(flags&VFS_WRITE)) ||
        ((flags&VFS_EXCLUSIVE) && !(flags&VFS_CREATE)) ||
        ((flags&VFS_OPEN_DIR) && (flags&~(VFS_OPEN_DIR|VFS_READ)))) return VFS_INVALID;
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    int slot=VFS_FIRST_FD; while(slot<VFS_FDS && s->files[slot].used) ++slot;
    if(slot==VFS_FDS) return VFS_NO_SPACE;
    struct vfs_location p;
    enum vfs_error e=resolve(v,s,path,&p);
    if(!e && (flags&VFS_EXCLUSIVE)) return VFS_EXISTS;
    if(e==VFS_NOT_FOUND && (flags&VFS_CREATE)) e=create_node(v,s,path,VFS_FILE,&p);
    if(e) return e;
    struct vfs_volume *volume=v->mounts[p.mount].volume;
    if(p.node->type==VFS_DIRECTORY && !(flags&VFS_OPEN_DIR)) return VFS_IS_DIR;
    if(p.node->type!=VFS_DIRECTORY && (flags&VFS_OPEN_DIR)) return VFS_NOT_DIR;
    if((flags&VFS_WRITE) && volume->read_only) return VFS_READ_ONLY;
    if((flags&VFS_READ) && p.node->type==VFS_FILE && !volume->ops->read) return VFS_UNSUPPORTED;
    if((flags&VFS_WRITE) && !volume->ops->write) return VFS_UNSUPPORTED;
    if(flags&VFS_TRUNCATE) {
        if(!volume->ops->truncate) return VFS_UNSUPPORTED;
        e=volume->ops->truncate(p.node); if(e) return e;
    }
    s->files[slot]=(struct vfs_file){.used=true,.flags=flags,.location=p};
    ++v->mounts[p.mount].users; *fd=slot; return VFS_OK;
}
static struct vfs_file *file(struct vfs *v,uint64_t pid,int fd) {
    struct vfs_session *s=session(v,pid,false);
    return s && fd>=VFS_FIRST_FD && fd<VFS_FDS && s->files[fd].used ? &s->files[fd]:0;
}
enum vfs_error vfs_core_close(struct vfs *v,uint64_t pid,int fd) {
    struct vfs_file *f=file(v,pid,fd); if(!f) return VFS_BAD_FD;
    --v->mounts[f->location.mount].users; *f=(struct vfs_file){0}; return VFS_OK;
}
static struct vfs_io transfer(struct vfs *v,uint64_t pid,int fd,void *buffer,size_t bytes,bool write) {
    struct vfs_file *f=file(v,pid,fd); if(!f) return (struct vfs_io){VFS_BAD_FD,0};
    if(!(f->flags&(write ? VFS_WRITE:VFS_READ))) return (struct vfs_io){VFS_ACCESS,0};
    if(f->location.node->type!=VFS_FILE) return (struct vfs_io){VFS_IS_DIR,0};
    if(bytes>VFS_IO_MAX || (bytes && (!buffer || (uintptr_t)buffer>UINTPTR_MAX-bytes))) return (struct vfs_io){VFS_INVALID,0};
    struct vfs_volume *volume=v->mounts[f->location.mount].volume;
    if(write && volume->read_only) return (struct vfs_io){VFS_READ_ONLY,0};
    if(!bytes) return (struct vfs_io){VFS_OK,0};
    uint64_t offset=write && (f->flags&VFS_APPEND) ? f->location.node->size:f->offset;
    if(offset>INT64_MAX || bytes>(uint64_t)INT64_MAX-offset) return (struct vfs_io){VFS_INVALID,0};
    struct vfs_io r=write ? volume->ops->write(f->location.node,offset,buffer,bytes) :
        volume->ops->read(f->location.node,offset,buffer,bytes);
    if(r.count>bytes) return (struct vfs_io){VFS_IO,0};
    if(r.count) f->offset=offset+r.count;
    return r;
}
struct vfs_io vfs_core_read(struct vfs *v,uint64_t pid,int fd,void *b,size_t n) { return transfer(v,pid,fd,b,n,false); }
struct vfs_io vfs_core_write(struct vfs *v,uint64_t pid,int fd,const void *b,size_t n) { return transfer(v,pid,fd,(void *)b,n,true); }
enum vfs_error vfs_core_seek(struct vfs *v,uint64_t pid,int fd,int64_t offset,enum vfs_whence whence,uint64_t *position) {
    struct vfs_file *f=file(v,pid,fd); if(!f) return VFS_BAD_FD;
    if(!position || (unsigned int)whence>VFS_SEEK_END) return VFS_INVALID;
    if(f->location.node->type==VFS_DIRECTORY && (whence!=VFS_SEEK_SET || offset)) return VFS_INVALID;
    uint64_t base=whence==VFS_SEEK_SET ? 0:whence==VFS_SEEK_CUR ? f->offset:f->location.node->size;
    uint64_t magnitude=offset<0 ? (uint64_t)(-(offset+1))+1:(uint64_t)offset;
    if(base>INT64_MAX || (offset<0 ? magnitude>base:magnitude>(uint64_t)INT64_MAX-base)) return VFS_INVALID;
    f->offset=offset<0 ? base-magnitude:base+magnitude; *position=f->offset; return VFS_OK;
}
enum vfs_error vfs_core_readdir(struct vfs *v,uint64_t pid,int fd,struct vfs_dirent *out,bool *end) {
    struct vfs_file *f=file(v,pid,fd); if(!f) return VFS_BAD_FD;
    if(!out || !end) return VFS_INVALID;
    if(f->location.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
    const struct vfs_ops *ops=v->mounts[f->location.mount].volume->ops;
    if(!ops->readdir) return VFS_UNSUPPORTED;
    enum vfs_error e=ops->readdir(f->location.node,f->offset,out,end);
    if(!e && !*end) ++f->offset;
    return e;
}
enum vfs_error vfs_core_stat(struct vfs *v,uint64_t pid,const char *path,struct vfs_stat *out) {
    if(!out) return VFS_INVALID;
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p);
    if(!e) *out=(struct vfs_stat){p.node->type,p.node->size};
    return e;
}
enum vfs_error vfs_core_mkdir(struct vfs *v,uint64_t pid,const char *path) {
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p);
    if(!e) return VFS_EXISTS;
    return e==VFS_NOT_FOUND ? create_node(v,s,path,VFS_DIRECTORY,&p):e;
}
enum vfs_error vfs_core_chdir(struct vfs *v,uint64_t pid,const char *path) {
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p); if(e) return e;
    if(p.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
    --v->mounts[s->cwd.mount].users; s->cwd=p; ++v->mounts[p.mount].users; return VFS_OK;
}
static enum vfs_error format_location(struct vfs *v,struct vfs_location p,char *out,size_t bytes) {
    if(!out || !bytes) return VFS_INVALID;
    char reverse[VFS_PATH_MAX]; size_t used=0;
    for(unsigned int depth=0;depth<VFS_PATH_MAX;++depth) {
        if(!p.mount && p.node==v->mounts[0].volume->root) {
            if(!used) reverse[used++]='/';
            if(bytes<=used) return VFS_NAME_LONG;
            for(size_t i=0;i<used;++i) out[i]=reverse[used-1-i];
            out[used]=0; return VFS_OK;
        }
        if(p.node==v->mounts[p.mount].volume->root && p.mount) p=v->mounts[p.mount].covered;
        size_t n=0; while(n<=VFS_NAME_MAX && p.node->name[n]) ++n;
        if(!n || n>VFS_NAME_MAX || used+n+1>=sizeof reverse) return VFS_NAME_LONG;
        while(n) reverse[used++]=p.node->name[--n];
        reverse[used++]='/'; p.node=p.node->parent;
    }
    return VFS_IO;
}
enum vfs_error vfs_core_getcwd(struct vfs *v,uint64_t pid,char *out,size_t bytes) {
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    return format_location(v,s->cwd,out,bytes);
}
enum vfs_error vfs_core_mount(struct vfs *v,uint64_t pid,const char *path,struct vfs_volume *volume) {
    if(!valid_volume(volume)) return VFS_INVALID;
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p); if(e) return e;
    if(p.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
    if(p.node==v->mounts[p.mount].volume->root) return VFS_BUSY;
    unsigned int slot=VFS_MOUNTS;
    for(unsigned int i=0;i<VFS_MOUNTS;++i) {
        if(v->mounts[i].used && v->mounts[i].volume==volume) return VFS_BUSY;
        if(!v->mounts[i].used && slot==VFS_MOUNTS) slot=i;
    }
    if(slot==VFS_MOUNTS) return VFS_NO_SPACE;
    v->mounts[slot]=(struct vfs_mountpoint){.used=true,.volume=volume,.covered=p}; return VFS_OK;
}
enum vfs_error vfs_core_unmount(struct vfs *v,uint64_t pid,const char *path) {
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p); if(e) return e;
    if(!p.mount || p.node!=v->mounts[p.mount].volume->root) return VFS_INVALID;
    if(v->mounts[p.mount].users) return VFS_BUSY;
    for(unsigned int i=1;i<VFS_MOUNTS;++i)
        if(v->mounts[i].used && v->mounts[i].covered.mount==p.mount) return VFS_BUSY;
    struct vfs_volume *volume=v->mounts[p.mount].volume;
    if(!volume->ops->sync && !volume->read_only) return VFS_UNSUPPORTED;
    if(volume->ops->sync) { e=volume->ops->sync(volume); if(e) return e; }
    v->mounts[p.mount]=(struct vfs_mountpoint){0}; return VFS_OK;
}
static bool descendant(struct vfs_location p,struct vfs_location ancestor) {
    if(p.mount!=ancestor.mount) return false;
    for(unsigned int depth=0;depth<VFS_PATH_MAX;++depth) {
        if(p.node==ancestor.node) return true;
        if(p.node==p.node->parent) return false;
        p.node=p.node->parent;
    }
    return true;
}
static bool referenced(struct vfs *v,struct vfs_location p) {
    if(p.node==v->mounts[p.mount].volume->root) return true;
    for(unsigned int i=0;i<VFS_PROCESSES;++i) if(v->sessions[i].pid) {
        if(descendant(v->sessions[i].cwd,p)) return true;
        for(unsigned int fd=VFS_FIRST_FD;fd<VFS_FDS;++fd)
            if(v->sessions[i].files[fd].used && descendant(v->sessions[i].files[fd].location,p)) return true;
    }
    for(unsigned int i=1;i<VFS_MOUNTS;++i) if(v->mounts[i].used && descendant(v->mounts[i].covered,p)) return true;
    return false;
}
static bool special_leaf(const char *path) {
    size_t n; if(path_length(path,&n)) return false;
    while(n && path[n-1]=='/') --n;
    size_t start=n; while(start && path[start-1]!='/') --start;
    return (n-start==1 && path[start]=='.') || (n-start==2 && path[start]=='.' && path[start+1]=='.');
}
enum vfs_error vfs_core_truncate(struct vfs *v,uint64_t pid,const char *path,uint64_t size) {
    if(size>INT64_MAX) return VFS_INVALID;
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p); if(e) return e;
    if(p.node->type!=VFS_FILE) return VFS_IS_DIR;
    struct vfs_volume *volume=v->mounts[p.mount].volume;
    if(volume->read_only) return VFS_READ_ONLY;
    if(volume->ops->resize) return volume->ops->resize(p.node,size);
    return !size && volume->ops->truncate ? volume->ops->truncate(p.node):VFS_UNSUPPORTED;
}
enum vfs_error vfs_core_rename(struct vfs *v,uint64_t pid,const char *from,const char *to) {
    if(special_leaf(from)) return VFS_INVALID;
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location source,dest; enum vfs_error e=resolve(v,s,from,&source); if(e) return e;
    e=resolve(v,s,to,&dest);
    if(!e) return same(source,dest) ? VFS_OK:VFS_EXISTS;
    if(e!=VFS_NOT_FOUND) return e;
    size_t length; e=path_length(to,&length); if(e) return e;
    if(to[length-1]=='/' && source.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
    char leaf[VFS_NAME_MAX+1]; e=parent_path(v,s,to,&dest,leaf); if(e) return e;
    if(source.mount!=dest.mount) return VFS_CROSS_DEVICE;
    struct vfs_volume *volume=v->mounts[source.mount].volume;
    if(volume->read_only) return VFS_READ_ONLY;
    if(referenced(v,source)) return VFS_BUSY;
    if(descendant(dest,source)) return VFS_INVALID;
    if(!volume->ops->rename) return VFS_UNSUPPORTED;
    return volume->ops->rename(source.node,dest.node,leaf);
}
enum vfs_error vfs_core_remove(struct vfs *v,uint64_t pid,const char *path,bool directory) {
    if(special_leaf(path)) return VFS_INVALID;
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p); if(e) return e;
    if(directory && p.node->type!=VFS_DIRECTORY) return VFS_NOT_DIR;
    if(!directory && p.node->type!=VFS_FILE) return VFS_IS_DIR;
    struct vfs_volume *volume=v->mounts[p.mount].volume;
    if(volume->read_only) return VFS_READ_ONLY;
    if(referenced(v,p)) return VFS_BUSY;
    return volume->ops->remove ? volume->ops->remove(p.node):VFS_UNSUPPORTED;
}
enum vfs_error vfs_core_sync(struct vfs *v,uint64_t pid,const char *path) {
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p); if(e) return e;
    struct vfs_volume *volume=v->mounts[p.mount].volume;
    return volume->ops->sync ? volume->ops->sync(volume):volume->read_only ? VFS_OK:VFS_UNSUPPORTED;
}
enum vfs_error vfs_core_sync_all(struct vfs *v) {
    if(!v || !v->ready) return VFS_CONTEXT;
    enum vfs_error result=VFS_OK;
    for(unsigned int i=0;i<VFS_MOUNTS;++i) if(v->mounts[i].used) {
        struct vfs_volume *volume=v->mounts[i].volume;
        enum vfs_error e=volume->ops->sync ? volume->ops->sync(volume):volume->read_only ? VFS_OK:VFS_UNSUPPORTED;
        if(e && !result) result=e;
    }
    return result;
}
enum vfs_error vfs_core_touch(struct vfs *v,uint64_t pid,const char *path) {
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p);
    if(e==VFS_NOT_FOUND) return create_node(v,s,path,VFS_FILE,&p);
    if(e) return e;
    return vfs_core_truncate(v,pid,path,p.node->size);
}
enum vfs_error vfs_core_copy_file(struct vfs *v,uint64_t pid,const char *from,const char *to) {
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location source,dest; enum vfs_error e=resolve(v,s,from,&source); if(e) return e;
    if(source.node->type!=VFS_FILE) return VFS_IS_DIR;
    e=resolve(v,s,to,&dest);
    if(!e && same(source,dest)) return VFS_INVALID;
    if(e && e!=VFS_NOT_FOUND) return e;
    int input,output; e=vfs_core_open(v,pid,from,VFS_READ,&input); if(e) return e;
    e=vfs_core_open(v,pid,to,VFS_WRITE|VFS_CREATE|VFS_TRUNCATE,&output);
    if(e) { (void)vfs_core_close(v,pid,input); return e; }
    unsigned char buffer[2048];
    for(;;) {
        struct vfs_io r=vfs_core_read(v,pid,input,buffer,sizeof buffer);
        if(r.error) { e=r.error; break; }
        if(!r.count) break;
        struct vfs_io w=vfs_core_write(v,pid,output,buffer,r.count);
        if(w.error || w.count!=r.count) { e=w.error ? w.error:VFS_IO; break; }
    }
    (void)vfs_core_close(v,pid,input); (void)vfs_core_close(v,pid,output); return e;
}
enum vfs_error vfs_core_statvfs(struct vfs *v,uint64_t pid,const char *path,struct vfs_space *out) {
    if(!out) return VFS_INVALID;
    struct vfs_session *s=session(v,pid,true); if(!s) return VFS_NO_SPACE;
    struct vfs_location p; enum vfs_error e=resolve(v,s,path,&p); if(e) return e;
    struct vfs_volume *volume=v->mounts[p.mount].volume;
    *out=(struct vfs_space){0}; return volume->ops->space ? volume->ops->space(volume,out):VFS_UNSUPPORTED;
}
size_t vfs_core_mount_snapshot(struct vfs *v,struct vfs_mount_info *out,size_t capacity) {
    if(!v || !v->ready || (!out && capacity)) return 0;
    size_t count=0;
    for(unsigned int i=0;i<VFS_MOUNTS;++i) if(v->mounts[i].used) {
        if(count<capacity) {
            struct vfs_volume *volume=v->mounts[i].volume;
            struct vfs_mount_info *m=&out[count]; *m=(struct vfs_mount_info){.read_only=volume->read_only};
            m->error=format_location(v,(struct vfs_location){i,volume->root},m->path,sizeof m->path);
            strncpy(m->type,volume->type ? volume->type:"fs",sizeof m->type-1);
            if(volume->device) strncpy(m->device,volume->device->name,sizeof m->device-1);
            if(!m->error) m->error=volume->ops->space ? volume->ops->space(volume,&m->space):VFS_UNSUPPORTED;
        }
        ++count;
    }
    return count;
}
void vfs_core_reap(struct vfs *v,uint64_t pid) {
    struct vfs_session *s=session(v,pid,false); if(!s) return;
    for(int fd=VFS_FIRST_FD;fd<VFS_FDS;++fd) if(s->files[fd].used) (void)vfs_core_close(v,pid,fd);
    --v->mounts[s->cwd.mount].users; *s=(struct vfs_session){0};
}
void vfs_core_stats(const struct vfs *v,struct vfs_stats *out) {
    if(!out) return;
    *out=(struct vfs_stats){0}; if(!v || !v->ready) return;
    for(unsigned int i=0;i<VFS_MOUNTS;++i) out->mounts+=v->mounts[i].used;
    for(unsigned int i=0;i<VFS_PROCESSES;++i) if(v->sessions[i].pid) {
        ++out->sessions;
        for(unsigned int fd=VFS_FIRST_FD;fd<VFS_FDS;++fd) out->descriptors+=v->sessions[i].files[fd].used;
    }
}
const char *vfs_error_name(enum vfs_error e) {
    static const char *const names[]={"OK","argumento invalido","nao encontrado","ja existe","nao e diretorio",
        "e diretorio","nome/caminho longo","limite de recursos","descritor invalido","acesso negado","volume ocupado",
        "somente leitura","nao suportado","erro de I/O","contexto invalido","diretorio nao vazio","volumes diferentes"};
    return (unsigned int)e<sizeof names/sizeof *names ? names[e]:"erro desconhecido";
}
