#include <ramfs.h>
#include <runtime.h>
static enum vfs_error lookup(struct vfs_node *parent,const char *name,struct vfs_node **out) {
    struct ramfs *fs=parent->volume->context;
    for(unsigned int i=1;i<RAMFS_NODES;++i) if(fs->nodes[i].used &&
        fs->nodes[i].node.parent==parent && !strcmp(name,fs->nodes[i].node.name)) {
        *out=&fs->nodes[i].node; return VFS_OK;
    }
    return VFS_NOT_FOUND;
}
static enum vfs_error create(struct vfs_node *parent,const char *name,enum vfs_type type,struct vfs_node **out) {
    struct vfs_node *found;
    if(lookup(parent,name,&found)==VFS_OK) return VFS_EXISTS;
    struct ramfs *fs=parent->volume->context;
    for(unsigned int i=1;i<RAMFS_NODES;++i) if(!fs->nodes[i].used) {
        struct ramfs_node *n=&fs->nodes[i]; *n=(struct ramfs_node){.used=true};
        n->node=(struct vfs_node){.volume=parent->volume,.parent=parent,.type=type};
        strcpy(n->node.name,name); *out=&n->node; return VFS_OK;
    }
    return VFS_NO_SPACE;
}
static struct vfs_io read_file(struct vfs_node *node,uint64_t offset,void *out,size_t bytes) {
    struct ramfs_node *n=(struct ramfs_node *)node;
    if(offset>=node->size) return (struct vfs_io){VFS_OK,0};
    if(bytes>node->size-offset) bytes=node->size-offset;
    const uint8_t *data=n->borrowed ? n->boot_data:n->data;
    memcpy(out,data+(size_t)offset,bytes); return (struct vfs_io){VFS_OK,bytes};
}
static struct vfs_io write_file(struct vfs_node *node,uint64_t offset,const void *in,size_t bytes) {
    struct ramfs_node *n=(struct ramfs_node *)node;
    if(n->borrowed) return (struct vfs_io){VFS_READ_ONLY,0};
    if(offset>=RAMFS_FILE_SIZE) return (struct vfs_io){VFS_NO_SPACE,0};
    bool partial=bytes>RAMFS_FILE_SIZE-offset;
    if(partial) bytes=RAMFS_FILE_SIZE-offset;
    if(offset>node->size) memset(n->data+(size_t)node->size,0,offset-node->size);
    memcpy(n->data+(size_t)offset,in,bytes);
    if(offset+bytes>node->size) node->size=offset+bytes;
    return (struct vfs_io){partial ? VFS_NO_SPACE:VFS_OK,bytes};
}
static enum vfs_error truncate_file(struct vfs_node *node) {
    struct ramfs_node *n=(struct ramfs_node *)node;
    if(n->borrowed) return VFS_READ_ONLY;
    memset(n->data,0,sizeof n->data); node->size=0; return VFS_OK;
}
static enum vfs_error readdir_node(struct vfs_node *parent,uint64_t index,struct vfs_dirent *out,bool *end) {
    struct ramfs *fs=parent->volume->context;
    for(unsigned int i=1;i<RAMFS_NODES;++i) if(fs->nodes[i].used && fs->nodes[i].node.parent==parent) {
        if(index) { --index; continue; }
        struct vfs_node *node=&fs->nodes[i].node;
        *out=(struct vfs_dirent){.type=node->type,.size=node->size}; strcpy(out->name,node->name);
        *end=false; return VFS_OK;
    }
    *out=(struct vfs_dirent){0}; *end=true; return VFS_OK;
}
static enum vfs_error sync_volume(struct vfs_volume *volume) { (void)volume; return VFS_OK; }
static enum vfs_error resize_file(struct vfs_node *node,uint64_t size) {
    struct ramfs_node *n=(struct ramfs_node *)node;
    if(n->borrowed) return VFS_READ_ONLY;
    if(size>RAMFS_FILE_SIZE) return VFS_NO_SPACE;
    if(size>node->size) memset(n->data+node->size,0,size-node->size);
    else memset(n->data+size,0,node->size-size);
    node->size=size; return VFS_OK;
}
static enum vfs_error rename_node(struct vfs_node *node,struct vfs_node *parent,const char *name) {
    struct vfs_node *found;
    if(lookup(parent,name,&found)==VFS_OK) return VFS_EXISTS;
    node->parent=parent; strcpy(node->name,name); return VFS_OK;
}
static enum vfs_error remove_node(struct vfs_node *node) {
    struct ramfs *fs=node->volume->context;
    if(node->type==VFS_DIRECTORY) for(unsigned int i=1;i<RAMFS_NODES;++i)
        if(fs->nodes[i].used && fs->nodes[i].node.parent==node) return VFS_NOT_EMPTY;
    memset((struct ramfs_node *)node,0,sizeof(struct ramfs_node)); return VFS_OK;
}
static enum vfs_error space_info(struct vfs_volume *volume,struct vfs_space *out) {
    struct ramfs *fs=volume->context;
    *out=(struct vfs_space){.block_size=RAMFS_FILE_SIZE,.free_known=true};
    if(volume->read_only) {
        for(unsigned int i=1;i<RAMFS_NODES;++i) if(fs->nodes[i].used && fs->nodes[i].node.type==VFS_FILE) out->total_bytes+=fs->nodes[i].node.size;
    } else {
        out->total_bytes=(RAMFS_NODES-1)*RAMFS_FILE_SIZE;
        for(unsigned int i=1;i<RAMFS_NODES;++i) {
            if(!fs->nodes[i].used) out->free_bytes+=RAMFS_FILE_SIZE;
            else if(fs->nodes[i].node.type==VFS_FILE && !fs->nodes[i].borrowed) out->free_bytes+=RAMFS_FILE_SIZE-fs->nodes[i].node.size;
        }
    }
    return VFS_OK;
}
static const struct vfs_ops ops={.lookup=lookup,.create=create,.read=read_file,.write=write_file,
    .truncate=truncate_file,.readdir=readdir_node,.sync=sync_volume,.resize=resize_file,.rename=rename_node,.remove=remove_node,.space=space_info};
void ramfs_init(struct ramfs *fs) {
    if(!fs) return;
    memset(fs,0,sizeof *fs);
    fs->volume=(struct vfs_volume){.ops=&ops,.root=&fs->nodes[0].node,.context=fs,.type="ramfs"};
    fs->nodes[0].used=true;
    fs->nodes[0].node=(struct vfs_node){.volume=&fs->volume,.parent=&fs->nodes[0].node,.type=VFS_DIRECTORY};
}
enum vfs_error ramfs_import_file(struct ramfs *fs,const char *name,const void *data,size_t bytes) {
    if(!fs || fs->volume.ops!=&ops || !name || !*name || bytes>INT64_MAX ||
        (bytes && (!data || (uintptr_t)data>UINTPTR_MAX-bytes))) return VFS_INVALID;
    if(fs->volume.read_only) return VFS_READ_ONLY;
    size_t length=0;
    while(length<=VFS_NAME_MAX && name[length]) {
        char ch=name[length];
        if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch=='_' || ch=='-' || ch=='.')) return VFS_INVALID;
        ++length;
    }
    if(length>VFS_NAME_MAX) return VFS_NAME_LONG;
    if(!strcmp(name,".") || !strcmp(name,"..")) return VFS_INVALID;
    struct vfs_node *node;
    enum vfs_error e=create(fs->volume.root,name,VFS_FILE,&node); if(e) return e;
    struct ramfs_node *n=(struct ramfs_node *)node;
    n->borrowed=true; n->boot_data=data; node->size=bytes; return VFS_OK;
}
