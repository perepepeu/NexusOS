#include <console.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
int main(void) {
    const size_t width=97,height=91,pitch=width*4+28,bytes=pitch*height;
    unsigned char *memory=malloc(bytes+64); assert(memory);
    for(size_t i=0;i<bytes+64;++i) memory[i]=0xa5;
    struct limine_framebuffer fb={.address=memory+32,.width=width,.height=height,.pitch=pitch,.bpp=32,
        .memory_model=LIMINE_FRAMEBUFFER_RGB,.red_mask_size=8,.red_mask_shift=16,
        .green_mask_size=8,.green_mask_shift=8,.blue_mask_size=8,.blue_mask_shift=0};
    struct limine_framebuffer *items[]={&fb};
    struct limine_framebuffer_response response={.framebuffer_count=1,.framebuffers=items};
    assert(console_init(&response));
    for(unsigned int i=0;i<200;++i) { console_write("NexusOS: rolagem e limites\n"); console_input("nexus> ","abcdefghijklmnopqrstuv",UINT32_MAX); }
    console_write_n("A\0B\r\b\n",6); console_clear();
    for(size_t i=0;i<32;++i) assert(memory[i]==0xa5 && memory[32+bytes+i]==0xa5);
    for(size_t y=0;y<height;++y) for(size_t x=width*4;x<pitch;++x) assert(memory[32+y*pitch+x]==0xa5);
    uint32_t *first=(void *)(memory+32); assert(*first==0x101820);
    fb.red_mask_shift=0; fb.blue_mask_shift=16; assert(console_init(&response)); assert(*first==0x201810);
    fb.blue_mask_shift=0; assert(!console_init(&response));
    console_write("sem framebuffer\n"); console_input("nexus> ","teste",0); console_clear();
    fb.blue_mask_shift=16; fb.pitch=width*4-1; assert(!console_init(&response));
    fb.pitch=pitch; fb.height=UINT64_MAX; assert(!console_init(&response));
    fb.height=height; fb.address=(void *)(uintptr_t)(UINTPTR_MAX-16); assert(!console_init(&response));
    assert(!console_init(0)); free(memory);
    puts("OK: framebuffer, RGB/BGR, pitch com padding, rolagem, cursores, guardas e metadados invalidos.");
}
