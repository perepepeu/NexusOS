#ifndef NEXUS_BOOTFS_H
#define NEXUS_BOOTFS_H
#include <ramfs.h>
#include <limine.h>
enum { BOOTFS_SCAN_MAX=64, BOOTFS_FILE_MAX=16*1024*1024 };
struct bootfs_report { unsigned int imported,rejected; bool truncated; };
/* Pure import, unmounted volume. Payload remains in Limine reserved memory. */
void bootfs_populate(struct ramfs *,const struct limine_module_response *,struct bootfs_report *);
bool bootfs_init(const struct limine_module_response *);
void bootfs_selftest(void);
#endif
