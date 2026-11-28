#include <runtime.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
int main(void) {
    unsigned char a[256], b[256];
    assert(memset(a,0xab,sizeof a)==a);
    for (size_t i=0;i<sizeof a;++i) assert(a[i]==0xab);
    assert(memcpy(b,a,sizeof a)==b && !memcmp(a,b,sizeof a));
    b[13]=0xff; assert(memcmp(a,b,sizeof a)<0 && memcmp(b,a,sizeof a)>0 && !memcmp(a,b,13));
    for (size_t from=0;from<32;++from) for(size_t to=0;to<32;++to) for(size_t n=0;n<65;++n) {
        unsigned char expected[256], saved[256];
        for(size_t i=0;i<256;++i) a[i]=expected[i]=saved[i]=(unsigned char)i;
        for(size_t i=0;i<n;++i) expected[to+i]=saved[from+i];
        assert(memmove(a+to,a+from,n)==a+to && !memcmp(a,expected,sizeof a));
    }
    char text[16]; assert(!strlen("") && strlen("NexusOS")==7);
    assert(strcpy(text,"NexusOS")==text && !strcmp(text,"NexusOS"));
    assert(strcmp("a","aa")<0 && strcmp("z","a")>0 && strcmp("\xff","\x7f")>0);
    assert(!strncmp("abc","abd",2) && strncmp("abc","abd",3)<0 && !strncmp("","",4));
    memset(text,0x55,sizeof text); assert(strncpy(text,"hi",8)==text && !memcmp(text,"hi\0\0\0\0\0\0",8) && text[8]==0x55);
    assert(strncpy(text,"123456",3)==text && !memcmp(text,"123",3) && !text[3]);
    assert(memset(0,1,0)==0 && memcpy(0,0,0)==0 && memmove(0,0,0)==0 && !memcmp(0,0,0) && !strncmp(0,0,0));
    puts("OK: runtime, memoria/strings, overlap em todas as direcoes, limites e bytes unsigned.");
}
