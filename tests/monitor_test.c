#include <monitor.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct process_info before[] = {{.pid=1,.cpu_ticks=7},{.pid=2,.cpu_ticks=100},{.pid=0,.cpu_ticks=3}};
    struct process_info after[] = {{.pid=1,.cpu_ticks=17},{.pid=2,.cpu_ticks=160},{.pid=0,.cpu_ticks=23},{.pid=9,.cpu_ticks=10}};
    struct monitor_row out[PROCESS_MAX];
    assert(monitor_build(before,3,after,4,100,out,PROCESS_MAX)==4);
    assert(out[0].process.pid==2 && out[0].ticks==60 && out[0].permille==600);
    assert(out[1].process.pid==0 && out[1].permille==200);
    assert(out[2].process.pid==1 && out[2].permille==100 && out[3].process.pid==9 && out[3].permille==100);
    before[0].cpu_ticks=UINT64_MAX-3; after[0].cpu_ticks=2;
    assert(monitor_build(before,1,after,1,10,out,1)==1 && out[0].ticks==6 && out[0].permille==600);
    assert(monitor_build(0,0,after,1,0,out,1)==1 && !out[0].permille && !out[0].ticks);
    after[0].cpu_ticks=UINT64_MAX;
    assert(monitor_build(0,0,after,1,100,out,1)==1 && out[0].permille==1000 && out[0].ticks==100);
    after[0].cpu_ticks=UINT64_MAX/2;
    assert(monitor_build(0,0,after,1,UINT64_MAX,out,1)==1 && out[0].permille==499);
    for (uint64_t elapsed=1; elapsed<200; ++elapsed) for (uint64_t ticks=0; ticks<=elapsed; ++ticks) {
        after[0].cpu_ticks=ticks;
        assert(monitor_build(0,0,after,1,elapsed,out,1)==1 && out[0].permille==ticks*1000/elapsed);
    }
    assert(!monitor_build(0,1,after,1,100,out,1));
    assert(!monitor_build(before,3,after,4,100,0,1));
    assert(!monitor_build(before,PROCESS_MAX+1,after,4,100,out,1));
    assert(!monitor_build(before,3,after,4,100,out,0));
    puts("OK: top, deltas por PID, ordenacao, PIDs novos, wrap, zero e percentuais sem overflow.");
}
