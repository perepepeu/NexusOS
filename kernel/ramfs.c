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
    memcpy(out,n->data+(size_t)offset,bytes); return (struct vfs_io){VFS_OK,bytes};
}
static struct vfs_io write_file(struct vfs_node *node,uint64_t offset,const void *in,size_t bytes) {
    struct ramfs_node *n=(struct ramfs_node *)node;
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
static const struct vfs_ops ops={lookup,create,read_file,write_file,truncate_file,readdir_node,sync_volume};
void ramfs_init(struct ramfs *fs) {
    if(!fs) return;
    memset(fs,0,sizeof *fs);
    fs->volume=(struct vfs_volume){.ops=&ops,.root=&fs->nodes[0].node,.context=fs};
    fs->nodes[0].used=true;
    fs->nodes[0].node=(struct vfs_node){.volume=&fs->volume,.parent=&fs->nodes[0].node,.type=VFS_DIRECTORY};
}
