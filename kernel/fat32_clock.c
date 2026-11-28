#include <fat32_internal.h>
#include <io.h>
static unsigned int cmos(unsigned int reg) { outb(0x70,(uint8_t)reg); return inb(0x71); }
static unsigned int decimal(unsigned int value,bool binary) {
    if(binary) return value;
    return (value&15)>9 || (value>>4)>9 ? 255:(value&15)+(value>>4)*10;
}
bool fat32_rtc(uint16_t *date,uint16_t *time) {
    for(unsigned int attempt=0;attempt<8;++attempt) {
        uint64_t flags=irq_save();
        if(cmos(10)&128) { irq_restore(flags); continue; }
        unsigned int second=cmos(0),minute=cmos(2),hour=cmos(4),day=cmos(7),month=cmos(8),year=cmos(9),century=cmos(0x32),mode=cmos(11);
        bool stable=second==cmos(0) && !(cmos(10)&128); irq_restore(flags);
        if(!stable) continue;
        bool binary=(mode&4)!=0,pm=(hour&128)!=0;
        second=decimal(second,binary); minute=decimal(minute,binary); hour=decimal(hour&127,binary);
        day=decimal(day,binary); month=decimal(month,binary); year=decimal(year,binary); century=decimal(century,binary);
        if(!(mode&2)) { if(hour<1 || hour>12) return false; hour=hour%12+(pm ? 12:0); }
        if(century<19 || century>21) century=20;
        year+=century*100;
        static const unsigned int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
        if(year<1980 || year>2107 || !month || month>12 || !day || hour>23 || minute>59 || second>59) return false;
        bool leap=!(year%4) && (year%100 || !(year%400));
        if(day>days[month-1]+(month==2 && leap)) return false;
        *date=(uint16_t)(((year-1980)<<9)|(month<<5)|day); *time=(uint16_t)((hour<<11)|(minute<<5)|(second/2)); return true;
    }
    return false;
}
