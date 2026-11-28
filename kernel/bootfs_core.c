#include <bootfs.h>
static const char *module_name(const struct limine_file *file) {
    if(file->string && file->string[0]) return file->string;
    if(!file->path) return 0;
    const char *name=file->path;
    for(size_t i=0;i<VFS_PATH_MAX;++i) {
        if(!file->path[i]) return name;
        if(file->path[i]=='/') name=file->path+i+1;
    }
    return 0;
}
void bootfs_populate(struct ramfs *fs,const struct limine_module_response *response,struct bootfs_report *out) {
    if(!fs || !out) return;
    *out=(struct bootfs_report){0}; ramfs_init(fs);
    if(response && response->modules) {
        out->truncated=response->module_count>BOOTFS_SCAN_MAX;
        for(uint64_t i=0;i<response->module_count && i<BOOTFS_SCAN_MAX;++i) {
            const struct limine_file *file=response->modules[i];
            if(!file || file->size>BOOTFS_FILE_MAX ||
                ramfs_import_file(fs,module_name(file),file->address,(size_t)file->size)!=VFS_OK) ++out->rejected;
            else ++out->imported;
        }
    } else if(response && response->module_count) out->rejected=1;
    fs->volume.read_only=true;
}
