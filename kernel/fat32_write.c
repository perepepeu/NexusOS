#include <fat32_internal.h>
#include <runtime.h>
static uint16_t r16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p)|((uint32_t)r16(p+2)<<16); }
static void w16(uint8_t *p,uint16_t n) { p[0]=n; p[1]=n>>8; }
static void w32(uint8_t *p,uint32_t n) { w16(p,n); w16(p+2,n>>16); }
static enum fat32_error error(struct fat32 *f,enum fat32_error e) { return fat32_set_error(f,e); }
static enum vfs_error map(enum fat32_error e) {
    return !e ? VFS_OK:e==FAT32_FULL || e==FAT32_LIMIT ? VFS_NO_SPACE:e==FAT32_READ_ONLY ? VFS_READ_ONLY:VFS_IO;
}
static enum fat32_error poison(struct fat32 *f) {
    f->poisoned=true; f->volume.read_only=true; f->cache_valid=false; f->tx.active=false;
    return error(f,FAT32_IO);
}
static enum fat32_error put_sector(struct fat32 *f,uint64_t relative,const uint8_t *data) {
    if(relative>=f->geometry.total) return error(f,FAT32_CORRUPT);
    f->block_error=block_write(f->volume.device,f->geometry.start+relative,1,data,f->geometry.sector_size);
    return f->block_error.error || f->block_error.completed!=1 ? poison(f):FAT32_OK;
}
static enum fat32_error flush(struct fat32 *f) {
    f->block_error=block_flush(f->volume.device);
    return f->block_error.error ? poison(f):FAT32_OK;
}
uint8_t *fat32_tx_find(struct fat32 *f,uint64_t relative) {
    if(f->tx.active) for(unsigned int i=0;i<f->tx.count;++i)
        if(f->tx.sectors[i].relative==relative) return f->tx.sectors[i].bytes;
    return 0;
}
static enum fat32_error stage(struct fat32 *f,uint64_t relative,uint8_t **out) {
    uint8_t *found=fat32_tx_find(f,relative); if(found) { *out=found; return FAT32_OK; }
    if(f->tx.count==FAT32_TX_SECTORS) return error(f,FAT32_LIMIT);
    struct fat32_tx_sector *s=&f->tx.sectors[f->tx.count];
    enum fat32_error e=fat32_read_sector(f,relative,s->bytes); if(e) return e;
    s->relative=relative; ++f->tx.count; *out=s->bytes; return FAT32_OK;
}
static enum fat32_error set_fat(struct fat32 *f,uint32_t cluster,uint32_t value) {
    struct fat32_geometry *g=&f->geometry; uint64_t offset=(uint64_t)cluster*4;
    unsigned int first=g->mirrored ? 0:g->active_fat,last=g->mirrored ? g->fat_count:first+1;
    for(unsigned int copy=first;copy<last;++copy) {
        uint64_t relative=g->reserved+(uint64_t)copy*g->fat_size+offset/g->sector_size;
        uint8_t *b; enum fat32_error e=stage(f,relative,&b); if(e) return e;
        unsigned int at=offset%g->sector_size;
        w32(b+at,(r32(b+at)&0xf0000000)|(value&0x0fffffff));
    }
    f->cache_valid=false; return FAT32_OK;
}
static enum fat32_error fsinfo_stage(struct fat32 *f) {
    struct fat32_geometry *g=&f->geometry;
    if(!g->fsinfo_valid) return FAT32_OK;
    uint32_t positions[2]={g->info_sector,(uint32_t)g->backup_sector+g->info_sector};
    for(unsigned int i=0;i<2;++i) {
        if(i && (!g->backup_sector || g->backup_sector==0xffff || positions[i]>=g->reserved)) continue;
        uint8_t *b; enum fat32_error e=stage(f,positions[i],&b); if(e) return e;
        if(r32(b)!=0x41615252 || r32(b+484)!=0x61417272 || r32(b+508)!=0xaa550000) continue;
        w32(b+488,f->tx.free_after); w32(b+492,f->tx.free_after ? f->tx.next_after:UINT32_MAX);
    }
    return FAT32_OK;
}
static enum fat32_error commit(struct fat32 *f,bool unlink_first) {
    enum fat32_error e=fsinfo_stage(f); if(e) return e;
    /* Grow: FAT -> directory. Free: unlink/size -> FAT. Each phase is durable.
     * FSInfo hints are written last. No promise of power-loss atomic rename. */
    for(unsigned int phase=0;phase<3;++phase) {
        for(unsigned int i=0;i<f->tx.count;++i) {
            struct fat32_tx_sector *s=&f->tx.sectors[i];
            unsigned int kind=s->relative<f->geometry.reserved ? 2:s->relative<f->geometry.data_start ? (unlink_first ? 1:0):(unlink_first ? 0:1);
            if(kind!=phase) continue;
            e=put_sector(f,s->relative,s->bytes); if(e) return e;
            if(s->relative>=f->geometry.data_start) { e=flush(f); if(e) return e; }
        }
        e=flush(f); if(e) return e;
        if(!phase && !unlink_first && f->checkpoint) f->checkpoint(f,1);
    }
    f->geometry.free_clusters=f->tx.free_after; f->geometry.free_known=true;
    f->geometry.next_free=f->tx.free_after ? f->tx.next_after:UINT32_MAX;
    f->geometry.next_known=f->tx.free_after!=0;
    f->tx.active=false; f->cache_valid=false; return FAT32_OK;
}
static enum fat32_error begin(struct fat32 *f) {
    if(f->poisoned) return error(f,FAT32_IO);
    if(!f->writable || f->volume.read_only) return error(f,FAT32_READ_ONLY);
    f->tx.active=true; f->tx.count=0; f->tx.allocated_count=0;
    f->tx.free_after=f->geometry.free_clusters;
    f->tx.next_after=f->geometry.next_known ? f->geometry.next_free:2;
    return FAT32_OK;
}
static enum vfs_error abort_op(struct fat32 *f,enum fat32_error e) {
    if(e==FAT32_IO) (void)poison(f);
    f->tx.active=false; f->cache_valid=false; return map(e);
}
/* Marking dirty uses its own durable write; preserve the prepared transaction. */
static enum fat32_error prepare_io(struct fat32 *f) {
    if(f->geometry.clean) {
        /* Prepared FAT sectors include FAT[1]; mark dirty in ALL of those too. */
        for(unsigned int i=0;i<f->tx.count;++i) {
            uint64_t s=f->tx.sectors[i].relative;
            if(s>=f->geometry.reserved && s<f->geometry.data_start && (s-f->geometry.reserved)%f->geometry.fat_size==0) {
                uint8_t *b=f->tx.sectors[i].bytes; w32(b+4,r32(b+4)&~(1u<<27));
            }
        }
        /* Do not destroy staged changes while persisting the dirty bit. */
        f->tx.active=false;
        unsigned int first=f->geometry.mirrored ? 0:f->geometry.active_fat;
        unsigned int last=f->geometry.mirrored ? f->geometry.fat_count:first+1;
        for(unsigned int i=first;i<last;++i) {
            uint64_t relative=f->geometry.reserved+(uint64_t)i*f->geometry.fat_size;
            enum fat32_error e=fat32_read_sector(f,relative,f->data); if(e) return poison(f);
            w32(f->data+4,r32(f->data+4)&~(1u<<27));
            e=put_sector(f,relative,f->data); if(e) return e;
        }
        enum fat32_error e=flush(f); if(e) return e;
        f->geometry.clean=false; f->cache_valid=false; f->tx.active=true;
    }
    return FAT32_OK;
}
static uint64_t cluster_sector(struct fat32 *f,uint32_t cluster) {
    return f->geometry.data_start+(uint64_t)(cluster-2)*f->geometry.sectors_per_cluster;
}
static enum fat32_error reserve(struct fat32 *f,unsigned int count) {
    if(count>FAT32_TX_CLUSTERS-f->tx.allocated_count) return error(f,FAT32_LIMIT);
    if(count>f->tx.free_after) return error(f,FAT32_FULL);
    uint32_t current=f->tx.next_after; if(current<2 || current>f->geometry.clusters+1) current=2;
    for(uint32_t scanned=0;count && scanned<f->geometry.clusters;++scanned) {
        uint32_t value; enum fat32_error e=fat32_get_fat(f,current,&value); if(e) return e;
        if(!value) {
            f->tx.allocated[f->tx.allocated_count++]=current;
            e=set_fat(f,current,0x0fffffff); if(e) return e;
            --f->tx.free_after; --count;
        }
        if(++current>f->geometry.clusters+1) current=2;
    }
    if(count) return error(f,FAT32_FULL);
    f->tx.next_after=current; return FAT32_OK;
}
static enum fat32_error zero_allocated(struct fat32 *f) {
    memset(f->data,0,f->geometry.sector_size);
    for(unsigned int i=0;i<f->tx.allocated_count;++i) for(unsigned int s=0;s<f->geometry.sectors_per_cluster;++s) {
        enum fat32_error e=put_sector(f,cluster_sector(f,f->tx.allocated[i])+s,f->data); if(e) return e;
    }
    return FAT32_OK;
}
static enum fat32_error at_cluster(struct fat32 *f,uint32_t first,uint32_t index,uint32_t *out) {
    uint32_t current=first;
    for(uint32_t i=0;i<index;++i) {
        uint32_t next; bool end; enum fat32_error e=fat32_next(f,current,&next,&end);
        if(e || end) return e ? e:error(f,FAT32_CORRUPT);
        current=next;
    }
    *out=current; return FAT32_OK;
}
static enum fat32_error extend(struct fat32 *f,uint32_t first,uint32_t old_count,uint32_t count,uint32_t *new_first) {
    *new_first=first;
    if(count<=old_count) return FAT32_OK;
    if(count>FAT32_CHAIN_LIMIT) return error(f,FAT32_LIMIT);
    unsigned int start=f->tx.allocated_count;
    enum fat32_error e=reserve(f,count-old_count); if(e) return e;
    uint32_t previous=0;
    if(old_count) { e=at_cluster(f,first,old_count-1,&previous); if(e) return e; }
    for(unsigned int i=start;i<f->tx.allocated_count;++i) {
        uint32_t c=f->tx.allocated[i];
        if(previous) { e=set_fat(f,previous,c); if(e) return e; }
        else *new_first=c;
        previous=c;
    }
    return FAT32_OK;
}
static enum fat32_error release_chain(struct fat32 *f,uint32_t first) {
    if(!first) return FAT32_OK;
    uint32_t count; enum fat32_error e=fat32_chain(f,first,&count); if(e) return e;
    uint32_t current=first;
    for(uint32_t i=0;i<count;++i) {
        uint32_t next; bool end; e=fat32_next(f,current,&next,&end); if(e) return e;
        e=set_fat(f,current,0); if(e) return e;
        ++f->tx.free_after;
        if(f->tx.next_after>current) f->tx.next_after=current;
        if(end) break;
        current=next;
    }
    return FAT32_OK;
}
static void timestamp(struct fat32 *f,uint8_t *entry,bool create) {
    uint16_t date=33,time=0; if(f->clock) (void)f->clock(&date,&time);
    if(create) { entry[13]=0; w16(entry+14,time); w16(entry+16,date); }
    w16(entry+18,date); w16(entry+22,time); w16(entry+24,date);
}
static void first_cluster(uint8_t *entry,uint32_t first) { w16(entry+20,first>>16); w16(entry+26,first); }
static enum fat32_error entry_stage(struct fat32 *f,struct fat32_node *n,uint8_t **out) {
    uint8_t *b; enum fat32_error e=stage(f,n->entry_sector,&b); if(e) return e;
    *out=b+n->entry_offset; return FAT32_OK;
}
static enum fat32_error update_entry(struct fat32 *f,struct fat32_node *n,uint32_t first,uint32_t size) {
    uint8_t *entry; enum fat32_error e=entry_stage(f,n,&entry); if(e) return e;
    first_cluster(entry,first); w32(entry+28,size); entry[11]|=32; timestamp(f,entry,false); return FAT32_OK;
}
static enum fat32_error touch_parent(struct fat32 *f,struct fat32_node *parent) {
    if(parent->node.parent==&parent->node) return FAT32_OK;
    uint8_t *entry; enum fat32_error e=entry_stage(f,parent,&entry); if(e) return e;
    timestamp(f,entry,false); return FAT32_OK;
}
static enum fat32_error dir_location(struct fat32 *f,struct fat32_node *dir,uint32_t index,uint64_t *relative,unsigned int *offset) {
    if(index>=FAT32_DIR_LIMIT) return error(f,FAT32_LIMIT);
    uint64_t byte=(uint64_t)index*32; uint32_t c;
    enum fat32_error e=at_cluster(f,dir->first,(uint32_t)(byte/f->geometry.cluster_size),&c); if(e) return e;
    *relative=cluster_sector(f,c)+(byte%f->geometry.cluster_size)/f->geometry.sector_size;
    *offset=byte%f->geometry.sector_size; return FAT32_OK;
}
static enum fat32_error free_slot(struct fat32 *f,struct fat32_node *dir,uint32_t *index,uint64_t *relative,unsigned int *offset,uint32_t *new_count) {
    *new_count=dir->chain_length;
    uint64_t entries=(uint64_t)dir->chain_length*f->geometry.cluster_size/32;
    if(entries>FAT32_DIR_LIMIT) return error(f,FAT32_LIMIT);
    uint64_t loaded=UINT64_MAX;
    for(uint32_t i=0;i<entries;++i) {
        enum fat32_error e=dir_location(f,dir,i,relative,offset); if(e) return e;
        if(loaded!=*relative) { e=fat32_read_sector(f,*relative,f->data); if(e) return e; loaded=*relative; }
        if(!f->data[*offset] || f->data[*offset]==0xe5) { *index=i; return FAT32_OK; }
    }
    if(entries==FAT32_DIR_LIMIT) return error(f,FAT32_FULL);
    uint32_t first; enum fat32_error e=extend(f,dir->first,dir->chain_length,dir->chain_length+1,&first); if(e) return e;
    uint32_t last; e=at_cluster(f,first,dir->chain_length,&last); if(e) return e;
    *index=(uint32_t)entries; *relative=cluster_sector(f,last); *offset=0; ++*new_count;
    uint8_t *b; e=stage(f,*relative,&b); if(!e) memset(b,0,f->geometry.sector_size);
    return e;
}
static enum fat32_error encode_name(const char *name,uint8_t out[11],char canonical[13]) {
    memset(out,' ',11); size_t base=0,ext=0; bool dot=false;
    for(size_t i=0;name[i];++i) {
        unsigned char c=(unsigned char)name[i];
        if(c=='.' && !dot && base) { dot=true; continue; }
        if(c<33 || c>=127) return FAT32_ARGUMENT;
        static const char illegal[]="\"*+,./:;<=>?[\\]|";
        for(size_t j=0;illegal[j];++j) if(c==(unsigned char)illegal[j]) return FAT32_ARGUMENT;
        if(c>='a' && c<='z') c-=32;
        if(!dot) { if(base==8) return FAT32_ARGUMENT; out[base++]=c; }
        else { if(ext==3) return FAT32_ARGUMENT; out[8+ext++]=c; }
    }
    if(!base || (dot && !ext)) return FAT32_ARGUMENT;
    memcpy(canonical,out,base); size_t n=base;
    if(ext) { canonical[n++]='.'; memcpy(canonical+n,out+8,ext); n+=ext; }
    canonical[n]=0; return FAT32_OK;
}
static unsigned char ascii_upper(unsigned char c) { return c>='a' && c<='z' ? c-32:c; }
/* Check ASCII 8.3 candidates against BOTH aliases and valid long names. Reject
 * namespace mutation in the presence of orphan/unknown LFN sets, rather than
 * reattaching them to a new SFN with an accidentally matching checksum. */
static enum vfs_error name_available(struct fat32 *f,struct fat32_node *dir,const char *canonical,uint32_t ignore) {
    uint16_t characters[260]; unsigned int expected=0,total=0; uint8_t sum=0; bool seen=false,bad=false;
    uint64_t loaded=UINT64_MAX,entries=(uint64_t)dir->chain_length*f->geometry.cluster_size/32;
    if(entries>FAT32_DIR_LIMIT) return VFS_NO_SPACE;
    static const unsigned int positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for(uint32_t i=0;i<entries;++i) {
        uint64_t relative; unsigned int offset; enum fat32_error e=dir_location(f,dir,i,&relative,&offset); if(e) return map(e);
        if(loaded!=relative) { e=fat32_read_sector(f,relative,f->data); if(e) return map(e); loaded=relative; }
        const uint8_t *p=f->data+offset;
        if(!p[0]) return seen ? VFS_UNSUPPORTED:VFS_OK;
        if(p[0]==0xe5) { if(seen) return VFS_UNSUPPORTED; continue; }
        if((p[11]&15)==15) {
            unsigned int ord=p[0]&31;
            if(p[0]&64) {
                if(seen) return VFS_UNSUPPORTED;
                seen=true; total=expected=ord; sum=p[13]; bad=false; memset(characters,0xff,sizeof characters);
            }
            if(!seen) { seen=true; bad=true; }
            if(!ord || ord>20 || (p[0]&0xa0) || ord!=expected || p[13]!=sum || p[12] || r16(p+26)) bad=true;
            if(ord>=1 && ord<=20) for(unsigned int j=0;j<13;++j) characters[(ord-1)*13+j]=r16(p+positions[j]);
            if(expected) --expected;
            continue;
        }
        if(seen) {
            uint8_t check=0; for(unsigned int j=0;j<11;++j) check=(uint8_t)((check>>1)|(check<<7))+p[j];
            if(bad || expected || !total || sum!=check || (p[11]&8)) return VFS_UNSUPPORTED;
            size_t n=strlen(canonical),j=0;
            while(j<n && characters[j]<128 && ascii_upper((unsigned char)characters[j])==ascii_upper((unsigned char)canonical[j])) ++j;
            if(i!=ignore && j==n && (characters[j]==0 || (j==total*13))) return VFS_EXISTS;
            seen=false;
        }
    }
    return seen ? VFS_UNSUPPORTED:VFS_OK;
}
static enum fat32_error erase_name(struct fat32 *f,struct fat32_node *n) {
    if(n->lfn_unsafe) return error(f,FAT32_ARGUMENT);
    struct fat32_node *parent=(struct fat32_node *)n->node.parent;
    for(unsigned int i=0;i<=n->lfn_count;++i) {
        uint64_t relative; unsigned int offset;
        enum fat32_error e=dir_location(f,parent,n->entry_index-i,&relative,&offset); if(e) return e;
        uint8_t *b; e=stage(f,relative,&b); if(e) return e;
        b[offset]=0xe5;
    }
    return FAT32_OK;
}
static enum fat32_error preflight(struct fat32 *f) {
    enum fat32_error e=fsinfo_stage(f); if(e) return e;
    e=prepare_io(f); if(e) return e;
    return zero_allocated(f);
}
enum fat32_error fat32_enable_write(struct fat32 *f,bool (*clock)(uint16_t *,uint16_t *)) {
    if(!f || !f->ready || f->poisoned) return FAT32_ARGUMENT;
    const struct block_device *d=f->volume.device;
    if(d->read_only || !d->ops.write || !d->ops.flush) return error(f,FAT32_READ_ONLY);
    if(!f->geometry.clean || !f->geometry.no_hard_error) return error(f,FAT32_DIRTY);
    if(f->geometry.clusters>FAT32_WRITE_CLUSTERS) return error(f,FAT32_LIMIT);
    struct fat32_geometry *g=&f->geometry;
    uint32_t free_count=0,next_free=UINT32_MAX;
    uint64_t entries=(uint64_t)g->clusters+2,sectors=(entries*4+g->sector_size-1)/g->sector_size;
    for(uint64_t i=0;i<sectors;++i) {
        enum fat32_error e=fat32_read_sector(f,g->fat_start+i,f->fat_cache); if(e) return e;
        if(g->mirrored) for(unsigned int copy=1;copy<g->fat_count;++copy) {
            e=fat32_read_sector(f,g->reserved+(uint64_t)copy*g->fat_size+i,f->data); if(e) return e;
            for(unsigned int j=0;j<g->sector_size/4 && i*(g->sector_size/4)+j<entries;++j)
                if((r32(f->data+j*4)&0xfffffff)!=(r32(f->fat_cache+j*4)&0xfffffff)) return error(f,FAT32_DIRTY);
        }
        for(unsigned int j=0;j<g->sector_size/4;++j) {
            uint64_t cluster=i*(g->sector_size/4)+j;
            if(cluster>=2 && cluster<entries && !(r32(f->fat_cache+j*4)&0xfffffff)) {
                ++free_count; if(next_free==UINT32_MAX) next_free=(uint32_t)cluster;
            }
        }
    }
    f->cache_valid=false; enum fat32_error e=flush(f); if(e) return e;
    g->free_clusters=free_count; g->free_known=true; g->next_free=next_free; g->next_known=free_count!=0;
    f->clock=clock; f->writable=true; f->volume.read_only=false; return FAT32_OK;
}
enum vfs_error fat32_create(struct vfs_node *parent,const char *name,enum vfs_type type,struct vfs_node **out) {
    struct fat32 *f=parent->volume->context; struct fat32_node *dir=(struct fat32_node *)parent;
    if(dir->attributes&1) return VFS_READ_ONLY;
    uint8_t encoded[11]; char canonical[13]; enum fat32_error e=encode_name(name,encoded,canonical); if(e) return VFS_INVALID;
    struct vfs_node *existing; enum vfs_error found=f->volume.ops->lookup(parent,name,&existing);
    if(found==VFS_OK) return VFS_EXISTS;
    if(found!=VFS_NOT_FOUND) return found;
    found=name_available(f,dir,canonical,UINT32_MAX); if(found) return found;
    struct fat32_node *n=fat32_node_slot(f); if(!n) return VFS_NO_SPACE;
    e=begin(f); if(e) return map(e);
    uint32_t first=0;
    if(type==VFS_DIRECTORY) { e=reserve(f,1); if(e) return abort_op(f,e); first=f->tx.allocated[0]; }
    uint32_t index,parent_count; uint64_t relative; unsigned int offset;
    uint8_t record[32]={0}; memcpy(record,encoded,11); record[11]=type==VFS_DIRECTORY ? 16:32;
    first_cluster(record,first); timestamp(f,record,true);
    if(type==VFS_DIRECTORY) {
        uint8_t *b; e=stage(f,cluster_sector(f,first),&b); if(e) return abort_op(f,e);
        memset(b,0,f->geometry.sector_size); memcpy(b,record,32); memset(b,' ',11); b[0]='.';
        memcpy(b+32,record,32); memset(b+32,' ',11); b[32]=b[33]='.';
        first_cluster(b+32,dir->node.parent==&dir->node ? 0:dir->first);
    }
    e=free_slot(f,dir,&index,&relative,&offset,&parent_count); if(e) return abort_op(f,e);
    uint8_t *slot; e=stage(f,relative,&slot); if(e) return abort_op(f,e);
    bool was_end=slot[offset]==0; memcpy(slot+offset,record,32);
    /* Consuming the end marker must leave a terminator after the new entry. */
    if(was_end && (uint64_t)(index+1)*32<(uint64_t)parent_count*f->geometry.cluster_size) {
        uint64_t next_sector; unsigned int next_offset;
        e=dir_location(f,dir,index+1,&next_sector,&next_offset); if(e) return abort_op(f,e);
        uint8_t *next; e=stage(f,next_sector,&next); if(e) return abort_op(f,e); next[next_offset]=0;
    }
    e=touch_parent(f,dir); if(e) return abort_op(f,e);
    e=preflight(f); if(e) return abort_op(f,e);
    e=commit(f,false); if(e) return abort_op(f,e);
    dir->chain_length=parent_count;
    if(n==&f->nodes[f->node_count]) ++f->node_count;
    *n=(struct fat32_node){.node={.volume=&f->volume,.parent=parent,.type=type},.first=first,
        .chain_length=type==VFS_DIRECTORY ? 1:0,.cursor=first,.used=true,.attributes=record[11],
        .entry_sector=relative,.entry_offset=offset,.entry_index=index};
    strcpy(n->node.name,canonical); *out=&n->node; return VFS_OK;
}
static enum fat32_error data_range(struct fat32 *f,uint32_t first,uint64_t offset,const uint8_t *input,uint64_t bytes) {
    uint32_t index=(uint32_t)(offset/f->geometry.cluster_size),current;
    enum fat32_error e=at_cluster(f,first,index,&current); if(e) return e;
    while(bytes) {
        uint32_t within=offset%f->geometry.cluster_size;
        uint64_t relative=cluster_sector(f,current)+within/f->geometry.sector_size;
        e=fat32_read_sector(f,relative,f->data); if(e) return e;
        size_t at=within%f->geometry.sector_size,part=f->geometry.sector_size-at;
        if(part>bytes) part=(size_t)bytes;
        if(input) { memcpy(f->data+at,input,part); input+=part; } else memset(f->data+at,0,part);
        e=put_sector(f,relative,f->data); if(e) return e;
        offset+=part; bytes-=part;
        if(bytes && offset%f->geometry.cluster_size==0) {
            uint32_t next; bool end; e=fat32_next(f,current,&next,&end);
            if(e || end) return e ? e:error(f,FAT32_CORRUPT);
            current=next;
        }
    }
    return FAT32_OK;
}
struct vfs_io fat32_write(struct vfs_node *node,uint64_t offset,const void *data,size_t bytes) {
    struct fat32_node *n=(struct fat32_node *)node; struct fat32 *f=node->volume->context;
    if(n->attributes&1) return (struct vfs_io){VFS_READ_ONLY,0};
    if(offset>UINT32_MAX || bytes>UINT32_MAX-offset) return (struct vfs_io){VFS_INVALID,0};
    if(!bytes) return (struct vfs_io){VFS_OK,0};
    enum fat32_error e=begin(f); if(e) return (struct vfs_io){map(e),0};
    uint64_t size=offset+bytes; if(size<node->size) size=node->size;
    uint32_t count=(uint32_t)((size+f->geometry.cluster_size-1)/f->geometry.cluster_size),first;
    e=extend(f,n->first,n->chain_length,count,&first); if(e) return (struct vfs_io){abort_op(f,e),0};
    e=update_entry(f,n,first,(uint32_t)size); if(e) return (struct vfs_io){abort_op(f,e),0};
    e=preflight(f); if(e) return (struct vfs_io){abort_op(f,e),0};
    if(offset>node->size) e=data_range(f,first,node->size,0,offset-node->size);
    if(!e) e=data_range(f,first,offset,data,bytes);
    if(!e) e=flush(f);
    if(e) return (struct vfs_io){abort_op(f,e),0};
    e=commit(f,false); if(e) return (struct vfs_io){abort_op(f,e),0};
    n->first=n->cursor=first; n->cursor_index=0; n->chain_length=count; n->attributes|=32; node->size=size;
    return (struct vfs_io){VFS_OK,bytes};
}
enum vfs_error fat32_resize(struct vfs_node *node,uint64_t size) {
    struct fat32_node *n=(struct fat32_node *)node; struct fat32 *f=node->volume->context;
    if((n->attributes|((struct fat32_node *)node->parent)->attributes)&1) return VFS_READ_ONLY;
    if(size>UINT32_MAX) return VFS_INVALID;
    enum fat32_error e=begin(f); if(e) return map(e);
    uint32_t count=(uint32_t)((size+f->geometry.cluster_size-1)/f->geometry.cluster_size),first=n->first;
    if(count>n->chain_length) e=extend(f,n->first,n->chain_length,count,&first);
    else if(count<n->chain_length) {
        uint32_t release=n->first;
        if(count) {
            uint32_t last; e=at_cluster(f,first,count-1,&last);
            bool end; if(!e) e=fat32_next(f,last,&release,&end);
            if(!e && end) e=error(f,FAT32_CORRUPT);
            if(!e) e=set_fat(f,last,0x0fffffff);
        } else first=0;
        if(!e) e=release_chain(f,release);
    }
    if(!e) e=update_entry(f,n,first,(uint32_t)size);
    if(e) return abort_op(f,e);
    e=preflight(f); if(e) return abort_op(f,e);
    if(size>node->size) e=data_range(f,first,node->size,0,size-node->size);
    if(!e) e=flush(f);
    if(e) return abort_op(f,e);
    e=commit(f,size<node->size); if(e) return abort_op(f,e);
    node->size=size; n->first=n->cursor=first; n->cursor_index=0; n->chain_length=count; n->attributes|=32; return VFS_OK;
}
enum vfs_error fat32_truncate(struct vfs_node *node) { return fat32_resize(node,0); }
enum vfs_error fat32_remove(struct vfs_node *node) {
    struct fat32_node *n=(struct fat32_node *)node; struct fat32 *f=node->volume->context;
    if((n->attributes|((struct fat32_node *)node->parent)->attributes)&1) return VFS_READ_ONLY;
    if(n->lfn_unsafe) return VFS_UNSUPPORTED;
    if(node->type==VFS_DIRECTORY) {
        struct vfs_dirent entry; bool end; enum vfs_error ve=f->volume.ops->readdir(node,0,&entry,&end);
        if(ve) return ve;
        if(!end) return VFS_NOT_EMPTY;
    }
    enum fat32_error e=begin(f); if(e) return map(e);
    e=erase_name(f,n); if(!e) e=release_chain(f,n->first);
    if(!e) e=touch_parent(f,(struct fat32_node *)node->parent);
    if(e) return abort_op(f,e);
    e=preflight(f); if(!e) e=commit(f,true);
    if(e) return abort_op(f,e);
    memset(n,0,sizeof *n); return VFS_OK;
}
enum vfs_error fat32_rename(struct vfs_node *node,struct vfs_node *parent,const char *name) {
    struct fat32_node *n=(struct fat32_node *)node,*dir=(struct fat32_node *)parent;
    struct fat32 *f=node->volume->context;
    if((n->attributes|dir->attributes|((struct fat32_node *)node->parent)->attributes)&1) return VFS_READ_ONLY;
    if(n->lfn_unsafe) return VFS_UNSUPPORTED;
    uint8_t encoded[11]; char canonical[13]; enum fat32_error e=encode_name(name,encoded,canonical); if(e) return VFS_INVALID;
    struct vfs_node *found; enum vfs_error ve=f->volume.ops->lookup(parent,name,&found);
    if(!ve) return VFS_EXISTS;
    if(ve!=VFS_NOT_FOUND) return ve;
    ve=name_available(f,dir,canonical,parent==node->parent ? n->entry_index:UINT32_MAX); if(ve) return ve;
    e=begin(f); if(e) return map(e);
    uint8_t record[32]; uint8_t *old;
    e=fat32_read_sector(f,n->entry_sector,f->data); if(e) return abort_op(f,e);
    memcpy(record,f->data+n->entry_offset,32); memcpy(record,encoded,11); record[12]=0;
    uint32_t index=n->entry_index,parent_count=dir->chain_length; uint64_t relative=n->entry_sector; unsigned int offset=n->entry_offset;
    if(parent!=node->parent) {
        e=free_slot(f,dir,&index,&relative,&offset,&parent_count); if(e) return abort_op(f,e);
        uint8_t *dest; e=stage(f,relative,&dest); if(e) return abort_op(f,e);
        bool was_end=dest[offset]==0; memcpy(dest+offset,record,32);
        if(was_end && (uint64_t)(index+1)*32<(uint64_t)parent_count*f->geometry.cluster_size) {
            uint64_t s; unsigned int at; e=dir_location(f,dir,index+1,&s,&at); if(e) return abort_op(f,e);
            uint8_t *b; e=stage(f,s,&b); if(e) return abort_op(f,e); b[at]=0;
        }
        if(node->type==VFS_DIRECTORY) {
            uint8_t *b; e=stage(f,cluster_sector(f,n->first),&b); if(e) return abort_op(f,e);
            if(memcmp(b,".          ",11) || memcmp(b+32,"..         ",11) || !(b[43]&16)) return abort_op(f,error(f,FAT32_CORRUPT));
            first_cluster(b+32,dir->node.parent==&dir->node ? 0:dir->first);
        }
        e=erase_name(f,n); if(e) return abort_op(f,e);
    } else {
        e=erase_name(f,n); if(e) return abort_op(f,e);
        e=entry_stage(f,n,&old); if(e) return abort_op(f,e); memcpy(old,record,32);
    }
    e=touch_parent(f,(struct fat32_node *)node->parent); if(!e) e=touch_parent(f,dir);
    if(e) return abort_op(f,e);
    e=preflight(f); if(!e) e=commit(f,false);
    if(e) return abort_op(f,e);
    dir->chain_length=parent_count; node->parent=parent; strcpy(node->name,canonical);
    n->entry_sector=relative; n->entry_offset=offset; n->entry_index=index; n->lfn_count=0; n->lfn_unsafe=false; return VFS_OK;
}
enum vfs_error fat32_sync(struct vfs_volume *volume) {
    struct fat32 *f=volume->context;
    if(f->poisoned) return VFS_IO;
    if(!f->writable) return VFS_OK;
    enum fat32_error e=flush(f); if(e) return VFS_IO;
    if(f->geometry.clean) return VFS_OK;
    e=begin(f); if(e) return map(e);
    e=fsinfo_stage(f);
    uint32_t value; if(!e) e=fat32_get_fat(f,1,&value);
    if(!e) e=set_fat(f,1,value|(1u<<27));
    if(e) return abort_op(f,e);
    /* Flush FSInfo before publishing clean bits in the FAT. */
    for(unsigned int pass=0;pass<2;++pass) {
        for(unsigned int i=0;i<f->tx.count;++i) {
            struct fat32_tx_sector *s=&f->tx.sectors[i];
            if((s->relative<f->geometry.reserved)!=(pass==0)) continue;
            e=put_sector(f,s->relative,s->bytes); if(e) return abort_op(f,e);
        }
        e=flush(f); if(e) return abort_op(f,e);
    }
    f->tx.active=false; f->cache_valid=false; f->geometry.clean=true; return VFS_OK;
}
