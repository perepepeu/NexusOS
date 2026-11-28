#include <fat32.h>
#include <runtime.h>
#include <fat32_internal.h>
#define fail fat32_set_error
#define sector fat32_read_sector
#define fat_value fat32_get_fat
#define next_cluster fat32_next
static const struct vfs_ops ops;
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p)|((uint32_t)le16(p+2)<<16); }
enum fat32_error fail(struct fat32 *f,enum fat32_error e) { f->error=e; return e; }
enum fat32_error sector(struct fat32 *f,uint64_t relative,uint8_t *out) {
    if(f->poisoned) return fail(f,FAT32_IO);
    if(relative>=f->geometry.total) return fail(f,FAT32_CORRUPT);
    uint8_t *pending=fat32_tx_find(f,relative);
    if(pending) { memcpy(out,pending,f->geometry.sector_size); return FAT32_OK; }
    f->block_error=block_read(f->volume.device,f->geometry.start+relative,1,out,BLOCK_MAX_SECTOR);
    return f->block_error.error || f->block_error.completed!=1 ? fail(f,FAT32_IO):FAT32_OK;
}
static bool cluster_valid(const struct fat32 *f,uint32_t n) { return n>=2 && n<=f->geometry.clusters+1; }
enum fat32_error fat_value(struct fat32 *f,uint32_t cluster,uint32_t *out) {
    if(f->poisoned) return fail(f,FAT32_IO);
    uint64_t offset=(uint64_t)cluster*4;
    uint64_t relative=f->geometry.fat_start+offset/f->geometry.sector_size;
    if(offset/ f->geometry.sector_size>=f->geometry.fat_size) return fail(f,FAT32_CORRUPT);
    uint8_t *pending=fat32_tx_find(f,relative);
    if(pending) { *out=le32(pending+offset%f->geometry.sector_size)&0x0fffffff; return FAT32_OK; }
    if(!f->cache_valid || f->cache_sector!=relative) {
        f->cache_valid=false;
        enum fat32_error e=sector(f,relative,f->fat_cache); if(e) return e;
        f->cache_sector=relative; f->cache_valid=true;
    }
    *out=le32(f->fat_cache+offset%f->geometry.sector_size)&0x0fffffff; return FAT32_OK;
}
enum fat32_error next_cluster(struct fat32 *f,uint32_t cluster,uint32_t *next,bool *end) {
    if(!cluster_valid(f,cluster)) return fail(f,FAT32_CORRUPT);
    enum fat32_error e=fat_value(f,cluster,next); if(e) return e;
    *end=*next>=0x0ffffff8;
    if(!*end && (!cluster_valid(f,*next) || *next>=0x0ffffff0)) return fail(f,FAT32_CORRUPT);
    return FAT32_OK;
}
/* Brent cycle detection: one forward traversal, constant memory, no recursion.
 * Validate through EOC, even when a directory contains an early end marker. */
enum fat32_error fat32_chain(struct fat32 *f,uint32_t first,uint32_t *length) {
    if(!f || !length || !cluster_valid(f,first)) return f ? fail(f,FAT32_CORRUPT):FAT32_ARGUMENT;
    uint32_t current=first,anchor=first,power=1,period=0;
    for(uint32_t count=1;count<=FAT32_CHAIN_LIMIT;++count) {
        uint32_t next; bool end; enum fat32_error e=next_cluster(f,current,&next,&end); if(e) return e;
        if(end) { *length=count; return FAT32_OK; }
        if(next==anchor || count>=f->geometry.clusters) return fail(f,FAT32_CYCLE);
        if(++period==power) { anchor=next; power*=2; period=0; }
        current=next;
    }
    return fail(f,FAT32_LIMIT);
}
static enum vfs_error verror(enum fat32_error e) { return e==FAT32_OK ? VFS_OK:e==FAT32_LIMIT ? VFS_NO_SPACE:VFS_IO; }
static enum fat32_error geometry(struct fat32 *f) {
    const uint8_t *b=f->data; struct fat32_geometry *g=&f->geometry;
    if(b[510]!=0x55 || b[511]!=0xaa || !((b[0]==0xeb && b[2]==0x90) || b[0]==0xe9)) return fail(f,FAT32_GEOMETRY);
    g->sector_size=le16(b+11); g->sectors_per_cluster=b[13]; g->reserved=le16(b+14);
    g->fat_count=b[16]; g->fat_size=le32(b+36); g->total=le32(b+32); g->root=le32(b+44);
    if(g->sector_size!=f->volume.device->sector_size || !g->sectors_per_cluster || g->sectors_per_cluster>128 ||
        (g->sectors_per_cluster&(g->sectors_per_cluster-1)) || !g->reserved || !g->fat_count || g->fat_count>4 ||
        !g->fat_size || !g->total || g->total>g->length || le16(b+17) || le16(b+19) || le16(b+22) ||
        !(b[21]==0xf0 || b[21]>=0xf8)) return fail(f,FAT32_GEOMETRY);
    if(le16(b+42)) return fail(f,FAT32_VERSION);
    g->cluster_size=g->sector_size*g->sectors_per_cluster;
    if(g->cluster_size>32768) return fail(f,FAT32_GEOMETRY);
    uint64_t overhead=(uint64_t)g->reserved+(uint64_t)g->fat_count*g->fat_size;
    if(overhead>=g->total) return fail(f,FAT32_GEOMETRY);
    g->clusters=(uint32_t)((g->total-overhead)/g->sectors_per_cluster);
    if(g->clusters<65525 || g->clusters>0x0fffffee) return fail(f,FAT32_TYPE);
    if((uint64_t)g->fat_size*g->sector_size/4<(uint64_t)g->clusters+2 || !cluster_valid(f,g->root)) return fail(f,FAT32_GEOMETRY);
    uint16_t flags=le16(b+40);
    if(flags&0xff70) return fail(f,FAT32_GEOMETRY);
    g->mirrored=!(flags&0x80); g->active_fat=g->mirrored ? 0:flags&15;
    if(g->active_fat>=g->fat_count) return fail(f,FAT32_GEOMETRY);
    g->fat_start=g->reserved+(uint64_t)g->active_fat*g->fat_size; g->data_start=overhead;
    uint16_t info=le16(b+48),backup=le16(b+50);
    g->info_sector=info; g->backup_sector=backup;
    if((info!=0xffff && info>=g->reserved) || (backup!=0xffff && backup>=g->reserved)) return fail(f,FAT32_GEOMETRY);
    uint8_t media=b[21];
    uint32_t value; enum fat32_error e=fat_value(f,0,&value); if(e) return e;
    if((value&255)!=media || (value&0x0fffff00)!=0x0fffff00) return fail(f,FAT32_CORRUPT);
    e=fat_value(f,1,&value); if(e) return e;
    if((value&0x03ffffff)!=0x03ffffff) return fail(f,FAT32_CORRUPT);
    g->clean=(value&(1u<<27))!=0; g->no_hard_error=(value&(1u<<26))!=0;
    if(info && info!=0xffff) {
        g->fsinfo_present=true; e=sector(f,info,f->data); if(e) return e;
        b=f->data;
        g->fsinfo_valid=le32(b)==0x41615252 && le32(b+484)==0x61417272 && le32(b+508)==0xaa550000;
        if(g->fsinfo_valid) {
            g->free_clusters=le32(b+488); g->next_free=le32(b+492);
            g->free_known=g->free_clusters!=UINT32_MAX && g->free_clusters<=g->clusters;
            g->next_known=cluster_valid(f,g->next_free);
        }
    }
    return FAT32_OK;
}
enum fat32_error fat32_open(struct fat32 *f,const struct block_device *device,uint64_t start,uint64_t length) {
    if(!f || !block_valid_device(device) || !length || start>=device->sectors || length>device->sectors-start) return FAT32_ARGUMENT;
    memset(f,0,sizeof *f);
    f->volume=(struct vfs_volume){.ops=&ops,.root=&f->nodes[0].node,.context=f,.device=device,.read_only=true,.type="fat32"};
    f->geometry=(struct fat32_geometry){.start=start,.length=length,.total=length};
    enum fat32_error e=sector(f,0,f->data); if(e) return e;
    e=geometry(f); if(e) return e;
    struct fat32_node *root=&f->nodes[0]; root->first=f->geometry.root;
    e=fat32_chain(f,root->first,&root->chain_length); if(e) return e;
    root->node=(struct vfs_node){.volume=&f->volume,.parent=&root->node,.type=VFS_DIRECTORY};
    root->cursor=root->first; f->node_count=1; f->ready=true; return FAT32_OK;
}
struct fat32_node *fat32_node_slot(struct fat32 *f) {
    for(unsigned int i=1;i<f->node_count;++i) if(!f->nodes[i].used) return &f->nodes[i];
    return f->node_count<FAT32_NODES ? &f->nodes[f->node_count]:0;
}
static unsigned char upper(unsigned char c) { return c>='a' && c<='z' ? c-32:c; }
static bool equal_name(const char *a,const char *b) {
    while(*a && upper((unsigned char)*a)==upper((unsigned char)*b)) { ++a; ++b; }
    return !*a && !*b;
}
static enum fat32_error short_name(const uint8_t *entry,char *out) {
    size_t n=0;
    for(unsigned int part=0;part<2;++part) {
        unsigned int begin=part ? 8:0,limit=part ? 11:8; bool padding=false;
        if(part && entry[8]!=' ') out[n++]='.';
        for(unsigned int i=begin;i<limit;++i) {
            uint8_t c=entry[i]; if(!i && c==5) c=0xe5;
            if(c==' ') { padding=true; continue; }
            if(padding || c<32 || c==127) return FAT32_CORRUPT;
            static const char forbidden[]="\"*+,./:;<=>?[\\]|";
            for(size_t j=0;forbidden[j];++j) if(c==(uint8_t)forbidden[j]) return FAT32_CORRUPT;
            if(c>='A' && c<='Z' && (entry[12]&(part ? 16:8))) c+=32;
            out[n++]=(char)c;
        }
        if(!part && !n) return FAT32_CORRUPT;
    }
    out[n]=0; return FAT32_OK;
}
struct directory_entry { char name[13]; enum vfs_type type; uint32_t first,size,index; uint64_t sector; unsigned int offset; uint8_t attr,lfn_count; bool lfn_unsafe; };
/* Deleted entries, labels, dot records and ALL LFN slots are never exposed as
 * files. Use the following SFN alias; neither valid nor malformed LFN is written. */
static enum vfs_error find_entry(struct fat32_node *directory,const char *name,uint64_t index,struct directory_entry *out,bool *end) {
    struct fat32 *f=directory->node.volume->context; uint32_t current=directory->first; unsigned int scanned=0;
    if(f->poisoned) return VFS_IO;
    unsigned int lfn_total=0,lfn_expected=0; uint8_t lfn_sum=0; bool lfn_seen=false,lfn_bad=false;
    for(uint32_t ci=0;ci<directory->chain_length;++ci) {
        uint64_t start=f->geometry.data_start+(uint64_t)(current-2)*f->geometry.sectors_per_cluster;
        for(unsigned int s=0;s<f->geometry.sectors_per_cluster;++s) {
            enum fat32_error e=sector(f,start+s,f->data); if(e) return verror(e);
            for(unsigned int offset=0;offset<f->geometry.sector_size;offset+=32) {
                if(++scanned>FAT32_DIR_LIMIT) return verror(fail(f,FAT32_LIMIT));
                const uint8_t *p=f->data+offset;
                if(!p[0]) { *end=true; return VFS_OK; }
                if(p[0]==0xe5 || (p[11]&8 && (p[11]&15)!=15)) { lfn_seen=false; lfn_total=lfn_expected=0; continue; }
                if((p[11]&15)==15) {
                    unsigned int ord=p[0]&31;
                    if(p[0]&64) { lfn_seen=true; lfn_total=lfn_expected=ord; lfn_sum=p[13]; lfn_bad=false; }
                    if(!lfn_seen) { lfn_seen=true; lfn_bad=true; }
                    if(!ord || ord>20 || (p[0]&0xa0) || ord!=lfn_expected || p[13]!=lfn_sum || p[12] || le16(p+26)) lfn_bad=true;
                    if(lfn_expected) --lfn_expected;
                    continue;
                }
                if(p[11]&0xc0) return verror(fail(f,FAT32_CORRUPT));
                if(p[0]=='.' && (p[1]==' ' || (p[1]=='.' && p[2]==' '))) continue;
                struct directory_entry entry={.type=(p[11]&16) ? VFS_DIRECTORY:VFS_FILE,
                    .first=((uint32_t)le16(p+20)<<16)|le16(p+26),.size=le32(p+28),.sector=start+s,.offset=offset,.index=scanned-1,.attr=p[11]};
                uint8_t checksum=0; for(unsigned int j=0;j<11;++j) checksum=(uint8_t)((checksum>>1)|(checksum<<7))+p[j];
                bool valid_lfn=lfn_seen && !lfn_bad && !lfn_expected && lfn_sum==checksum && lfn_total;
                entry.lfn_count=valid_lfn ? lfn_total:0; entry.lfn_unsafe=lfn_seen && !valid_lfn;
                lfn_seen=false; lfn_total=lfn_expected=0;
                e=short_name(p,entry.name); if(e) return verror(fail(f,e));
                if(entry.first>0x0fffffff || (entry.type==VFS_DIRECTORY ? !cluster_valid(f,entry.first):
                    (entry.size ? !cluster_valid(f,entry.first):entry.first!=0))) return verror(fail(f,FAT32_CORRUPT));
                if(entry.type==VFS_DIRECTORY && entry.size) return verror(fail(f,FAT32_CORRUPT));
                if(name ? equal_name(name,entry.name):index--==0) { *out=entry; *end=false; return VFS_OK; }
            }
        }
        if(ci+1<directory->chain_length) {
            uint32_t next; bool chain_end; enum fat32_error e=next_cluster(f,current,&next,&chain_end);
            if(e || chain_end) return verror(e ? e:fail(f,FAT32_CORRUPT));
            current=next;
        }
    }
    *end=true; return VFS_OK;
}
static enum vfs_error lookup(struct vfs_node *parent,const char *name,struct vfs_node **out) {
    struct fat32 *f=parent->volume->context;
    if(f->poisoned) return VFS_IO;
    for(unsigned int i=1;i<f->node_count;++i) if(f->nodes[i].used && f->nodes[i].node.parent==parent && equal_name(name,f->nodes[i].node.name)) {
        *out=&f->nodes[i].node; return VFS_OK;
    }
    struct directory_entry entry; bool end; enum vfs_error e=find_entry((struct fat32_node *)parent,name,0,&entry,&end);
    if(e || end) return e ? e:VFS_NOT_FOUND;
    struct fat32_node *n=fat32_node_slot(f); if(!n) return VFS_NO_SPACE;
    uint32_t length=0;
    if(entry.first) {
        enum fat32_error fe=fat32_chain(f,entry.first,&length); if(fe) return verror(fe);
    }
    if(entry.type==VFS_FILE && length!=(entry.size+(uint64_t)f->geometry.cluster_size-1)/f->geometry.cluster_size)
        return verror(fail(f,FAT32_CORRUPT));
    /* Directory links to an ancestor are corrupt, not an alternate route to it. */
    if(entry.type==VFS_DIRECTORY) {
        struct vfs_node *p=parent;
        for(unsigned int depth=0;depth<FAT32_NODES;++depth) {
            if(((struct fat32_node *)p)->first==entry.first) return verror(fail(f,FAT32_CYCLE));
            if(p==p->parent) break;
            p=p->parent;
        }
    }
    if(n==&f->nodes[f->node_count]) ++f->node_count;
    *n=(struct fat32_node){.node={.volume=&f->volume,.parent=parent,.type=entry.type,.size=entry.size},
        .first=entry.first,.chain_length=length,.cursor=entry.first,.entry_sector=entry.sector,.entry_offset=entry.offset,
        .entry_index=entry.index,.attributes=entry.attr,.lfn_count=entry.lfn_count,.lfn_unsafe=entry.lfn_unsafe,.used=true};
    strcpy(n->node.name,entry.name); *out=&n->node; return VFS_OK;
}
static struct vfs_io read_file(struct vfs_node *node,uint64_t offset,void *buffer,size_t bytes) {
    struct fat32_node *n=(struct fat32_node *)node; struct fat32 *f=node->volume->context;
    if(f->poisoned) return (struct vfs_io){VFS_IO,0};
    if(offset>=node->size) return (struct vfs_io){VFS_OK,0};
    if(bytes>node->size-offset) bytes=node->size-offset;
    uint32_t index=(uint32_t)(offset/f->geometry.cluster_size),current=n->first,at=0;
    if(index>=n->cursor_index) { at=n->cursor_index; current=n->cursor; }
    for(;at<index;++at) {
        bool end; uint32_t next; enum fat32_error e=next_cluster(f,current,&next,&end);
        if(e || end) return (struct vfs_io){verror(e ? e:fail(f,FAT32_CORRUPT)),0};
        current=next;
    }
    size_t done=0;
    while(done<bytes) {
        n->cursor=current; n->cursor_index=index;
        uint32_t within=(uint32_t)(offset%f->geometry.cluster_size),s=within/f->geometry.sector_size;
        uint64_t relative=f->geometry.data_start+(uint64_t)(current-2)*f->geometry.sectors_per_cluster+s;
        enum fat32_error e=sector(f,relative,f->data); if(e) return (struct vfs_io){verror(e),done};
        size_t part=f->geometry.sector_size-within%f->geometry.sector_size;
        if(part>bytes-done) part=bytes-done;
        memcpy((uint8_t *)buffer+done,f->data+within%f->geometry.sector_size,part); done+=part; offset+=part;
        if(done<bytes && offset%f->geometry.cluster_size==0) {
            bool end; uint32_t next; e=next_cluster(f,current,&next,&end);
            if(e || end) return (struct vfs_io){verror(e ? e:fail(f,FAT32_CORRUPT)),done};
            current=next; ++index;
        }
    }
    return (struct vfs_io){VFS_OK,done};
}
static enum vfs_error readdir_node(struct vfs_node *node,uint64_t index,struct vfs_dirent *out,bool *end) {
    struct directory_entry entry; enum vfs_error e=find_entry((struct fat32_node *)node,0,index,&entry,end);
    if(!e && !*end) { *out=(struct vfs_dirent){.type=entry.type,.size=entry.size}; strcpy(out->name,entry.name); }
    return e;
}
static enum vfs_error space_info(struct vfs_volume *volume,struct vfs_space *out) {
    struct fat32 *f=volume->context; if(f->poisoned) return VFS_IO;
    *out=(struct vfs_space){.total_bytes=(uint64_t)f->geometry.clusters*f->geometry.cluster_size,
        .free_bytes=f->geometry.free_known ? (uint64_t)f->geometry.free_clusters*f->geometry.cluster_size:0,
        .block_size=f->geometry.cluster_size,.free_known=f->geometry.free_known,.approximate=!f->writable};
    return VFS_OK;
}
static const struct vfs_ops ops={.lookup=lookup,.read=read_file,.readdir=readdir_node,.sync=fat32_sync,
    .create=fat32_create,.write=fat32_write,.truncate=fat32_truncate,.resize=fat32_resize,.rename=fat32_rename,.remove=fat32_remove,.space=space_info};
const char *fat32_error_name(enum fat32_error e) {
    static const char *const names[]={"OK","intervalo/dispositivo invalido","BPB/geometria invalida","volume nao e FAT32",
        "versao FAT32 nao suportada","erro de I/O","cadeia/entrada invalida","referencia circular","limite de percurso",
        "disco cheio","somente leitura","volume sujo/inconsistente"};
    return (unsigned int)e<sizeof names/sizeof *names ? names[e]:"erro desconhecido";
}
