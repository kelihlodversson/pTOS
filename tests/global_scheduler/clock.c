/* Actual AUTO fixture deadlines with hosted GEMDOS/file/process endpoints.
 * No native clock or PROCESS execution is modeled as runtime evidence. */
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#define main global_fixture_main
#include "boot.c"
#undef main
#undef assert
#define assert(c) do { if(!(c)) { fprintf(stderr,"scheduler clock:%d: %s\n",__LINE__,#c); abort(); } } while(0)
static BASEPAGE basepage;
BASEPAGE *_base=&basepage;
static long disk[8];
static unsigned long clock_value, initial;
static unsigned reads, yields;
static short stopped;
static jmp_buf stop;
long Ssystem(short mode,long address,long unused)
{
    assert(address==0x4baL && !unused);
    if(mode==2) return 0x0100; /* Old code must fail rather than hang this test. */
    assert(mode==S_GETLVAL); reads++; clock_value+=100;
    return (long)clock_value;
}
long Syield(void) { assert(++yields<=20); return 0; }
long Fopen(const char *path,short mode) { assert(!strcmp(path,LOG)); (void)mode; return 9; }
long Fcreate(const char *path,short mode) { return Fopen(path,mode); }
long Fread(long fd,long size,void *out)
{ assert(fd==9 && size==sizeof(disk)); memcpy(out,disk,sizeof(disk)); return size; }
long Fwrite(long fd,long size,const void *in)
{ assert(fd==9 && size==sizeof(disk)); memcpy(disk,in,sizeof(disk)); return size; }
long Fclose(long fd) { assert(fd==9); return 0; }
long Cconws(const char *text) { (void)text; return 0; }
long Cconout(short c) { (void)c; return 0; }
void Pterm(short code) { stopped=code; longjmp(stop,1); }
long Pgetpid(void) { return 42; }
long Pgetppid(void) { return 0; }
long Pexec(short mode,const char *path,const void *tail,const void *environment)
{ assert(mode==100 && !strcmp(path,"C:\\GLOBALCH.TOS")); (void)tail; (void)environment; return 77; }
long Pwaitpid(short pid,short mode,long usage)
{ assert(pid==77 && !mode && !usage); return (77L<<16)|29; }
long Mfree(void *address) { (void)address; abort(); }
int main(void)
{
    static unsigned scenario, wrap; /* survive the modeled terminal longjmp */
    for(scenario=0;scenario<3;scenario++) for(wrap=0;wrap<2;wrap++) {
        memset(&basepage,0,sizeof(basepage)); memset(data,0,sizeof(data)); memset(disk,0,sizeof(disk));
        disk[0]=MAGIC; disk[1]=41; disk[2]=42; disk[3]=scenario==1 ? 2:1;
        role=scenario==1 ? 2:0;
        if(!scenario) { basepage.p_cmdlin[0]=1; basepage.p_cmdlin[1]='g'; }
        initial=clock_value=wrap ? ULONG_MAX-500:0;
        reads=yields=0; stopped=0;
        if(!setjmp(stop)) { global_fixture_main(); abort(); }
        assert(stopped==(scenario==0 ? 28:scenario==1 ? 10:20));
        assert(disk[6]==stopped && reads==11 && yields==10);
        assert(clock_value-initial==1100UL); /* start sample + exact1000-tick deadline */
    }
    puts("Actual scheduler AUTO fixture: three stalled waits expire at1000 ticks, unsigned wrap preserved: PASS (host clock model)");
    return 0;
}
