#include <syscall.h>
#include <stdbool.h>
#define CHECK(expr,code) do { if(!(expr)) { static const char fail[]="FSIO-CHECK-FAILED\n"; (void)sys_write(2,fail,sizeof fail-1); sys_exit(code); } } while(0)
static unsigned char data[8192] __attribute__((aligned(4096)));
static char own[64],file[80],shared[64],unterminated[USER_PATH_MAX];
static struct user_stat st;
static struct user_dirent ent,first;
static const char ro[]="readonly-buffer";
static size_t length(const char *s) { size_t n=0; while(s[n]) ++n; return n; }
static void copy(char *out,const char *in) { while((*out++=*in++)) {} }
static int same(const char *a,const char *b) { while(*a && *a==*b) { ++a; ++b; } return *a==*b; }
static void name_for(char *out,const char *base,uint64_t pid) {
    copy(out,base); size_t n=length(out); out[n++]='/'; out[n++]='P'; char digits[20]; unsigned int count=0;
    do { digits[count++]='0'+pid%10; pid/=10; } while(pid);
    while(count) out[n++]=digits[--count];
    out[n]=0;
}
int elf_main(const uint64_t *stack) {
    uint16_t cs; __asm__ volatile("mov %%cs,%0":"=r"(cs));
    CHECK((cs&3)==3 && stack[0]==1,101);
    const char *arg0=(const char *)(uintptr_t)stack[1]; bool ram=arg0[0]=='r' && arg0[1]=='a' && arg0[2]=='m';
    const char *base=ram ? "/tmp":"/mnt"; unsigned int block=ram ? 1024:4096; uint64_t total=3ull*block;
    uint64_t pid=(uint64_t)sys_getpid(); CHECK(pid>1 && pid<1000000,102);
    CHECK(sys_read(4,data,1)==-USER_EBADF && sys_close(4)==-USER_EBADF,103);
    CHECK(sys_open(0,USER_O_READ)==-USER_EFAULT,104);
    CHECK(sys_open((const char *)0xffffffff80000000ull,USER_O_READ)==-USER_EFAULT,105);
    for(unsigned int i=0;i<sizeof unterminated;++i) unterminated[i]='x';
    CHECK(sys_open(unterminated,USER_O_READ)==-USER_ENAMETOOLONG,106);
    CHECK(nexus_syscall3(USER_SYSCALL_OPEN,(uintptr_t)base,1ull<<40,0)==-USER_EINVAL,107);
    name_for(own,base,pid); int64_t result=sys_mkdir(own); CHECK(!result || result==-USER_EEXIST,108);
    CHECK(sys_chdir(own)==0 && sys_getcwd((char *)data,256)>0 && same((char *)data,own),109);
    copy(file,own); copy(file+length(file),"/DATA.BIN"); copy(shared,base); copy(shared+length(shared),"/SHARED.BIN");
    int64_t fd=sys_open("DATA.BIN",USER_O_READ|USER_O_WRITE|USER_O_CREATE|USER_O_TRUNCATE); CHECK(fd==3,110);
    for(unsigned int b=0;b<3;++b) {
        for(unsigned int i=0;i<block;++i) data[i]=(unsigned char)(pid*17+(b*block+i)*7);
        CHECK(sys_write((uint64_t)fd,data,block)==block,111);
    }
    CHECK(!sys_stat(file,&st) && st.size==total && st.type==USER_DT_FILE && !st.reserved,112);
    CHECK(sys_seek(fd,0,USER_SEEK_SET)==0,113);
    for(unsigned int b=0;b<3;++b) {
        CHECK(sys_read(fd,data,block)==block,114);
        for(unsigned int i=0;i<block;++i) CHECK(data[i]==(unsigned char)(pid*17+(b*block+i)*7),115);
    }
    CHECK(sys_read(fd,data,1)==0 && sys_seek(fd,0,USER_SEEK_CUR)==(int64_t)total,116);
    CHECK(sys_seek(fd,-1,USER_SEEK_SET)==-USER_EINVAL && sys_seek(fd,INT64_MIN,USER_SEEK_CUR)==-USER_EINVAL,117);
    CHECK(sys_seek(fd,0,999)==-USER_EINVAL,118);
    CHECK(sys_write(fd,0,0)==0 && sys_read(fd,(void *)UINT64_MAX,0)==0,119);
    CHECK(sys_write(fd,data,USER_IO_MAX+1)==-USER_EINVAL,120);
    CHECK(sys_read(fd,(void *)(uintptr_t)ro,1)==-USER_EFAULT,121);
    CHECK(sys_write(fd,(void *)0x0100000000000000ull,1)==-USER_EFAULT,122);
    CHECK(sys_write(fd,(void *)(UINT64_MAX-7),16)==-USER_EFAULT,123);
    CHECK(sys_write(fd,(void *)0x803fffull,2)==-USER_EFAULT && sys_read(fd,(void *)0x803fffull,2)==-USER_EFAULT,124);
    CHECK(sys_seek(fd,0,USER_SEEK_CUR)==(int64_t)total,125);
    size_t n=length(file); char *edge=(char *)(uintptr_t)(0x804000-n);
    for(size_t i=0;i<n;++i) edge[i]=file[i];
    CHECK(sys_open(edge,USER_O_WRITE|USER_O_TRUNCATE)==-USER_EFAULT,126);
    CHECK(!sys_stat(file,&st) && st.size==total,127);
    CHECK(sys_stat(file,(struct user_stat *)(uintptr_t)ro)==-USER_EFAULT,128);
    CHECK(sys_mkdir((const char *)0xffffffff80000000ull)==-USER_EFAULT,129);
    CHECK(sys_unlink(file)==-USER_EBUSY,130);
    int64_t read_only=sys_open("DATA.BIN",USER_O_READ); CHECK(read_only>=4,131);
    CHECK(sys_write(read_only,data,1)==-USER_EBADF && sys_close(read_only)==0 && sys_close(read_only)==-USER_EBADF,132);
    int64_t write_only=sys_open("DATA.BIN",USER_O_WRITE); CHECK(write_only>=4,133);
    CHECK(sys_read(write_only,data,1)==-USER_EBADF && sys_close(write_only)==0,134);
    CHECK(sys_mkdir("SUB")==0 || sys_stat("SUB",&st)==0,135);
    int64_t tmp=sys_open("SUB/DEL.TXT",USER_O_WRITE|USER_O_CREATE|USER_O_EXCLUSIVE); CHECK(tmp>=4,136);
    CHECK(sys_write(tmp,"deleted",7)==7 && sys_close(tmp)==0 && sys_unlink("SUB/DEL.TXT")==0 && sys_rmdir("SUB")==0,137);
    CHECK(sys_open(".",USER_O_READ)==-USER_EISDIR && sys_open("DATA.BIN",USER_O_READ|USER_O_DIRECTORY)==-USER_ENOTDIR,138);
    int64_t dir=sys_open(base,USER_O_READ|USER_O_DIRECTORY); CHECK(dir>=4,139);
    CHECK(sys_readdir(dir,&first)==1 && !first.reserved,140);
    CHECK(sys_seek(dir,0,USER_SEEK_SET)==0,141);
    CHECK(sys_readdir(dir,(struct user_dirent *)(uintptr_t)ro)==-USER_EFAULT,142);
    CHECK(sys_readdir(dir,&ent)==1 && same(first.name,ent.name),143);
    bool zero=false; for(unsigned int i=0;i<32;++i) { if(!ent.name[i]) zero=true; if(zero) CHECK(!ent.name[i],144); }
    unsigned int entries=1; while((result=sys_readdir(dir,&ent))==1) CHECK(++entries<200,145);
    CHECK(result==0 && sys_readdir(fd,&ent)==-USER_ENOTDIR,146);
    int64_t append=sys_open(shared,USER_O_WRITE|USER_O_APPEND); CHECK(append>=5,147);
    for(uint64_t sequence=0;sequence<16;++sequence) {
        uint64_t *words=(uint64_t *)data; words[0]=pid; words[1]=sequence;
        for(unsigned int i=16;i<64;++i) data[i]=(unsigned char)(pid+sequence+i);
        CHECK(sys_write(append,data,64)==64,148);
        CHECK(sys_sleep(1)==0,149);
    }
    CHECK(sys_sync(base)==0 && sys_getcwd((char *)(uintptr_t)ro,16)==-USER_EFAULT,150);
    int64_t opened[32]; unsigned int count=0;
    while((result=sys_open(file,USER_O_READ))>=0) { CHECK(count<32,151); opened[count++]=result; }
    CHECK(result==-USER_ENOSPC && count>0,152);
    while(count) CHECK(sys_close(opened[--count])==0,153);
    static const char done[]="FSIO USER PASS\n"; CHECK(sys_write(1,done,sizeof done-1)==sizeof done-1,154);
    return 0;
}
