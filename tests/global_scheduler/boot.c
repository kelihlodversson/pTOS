/* Actual user-mode AUTO launch/child/check programs.
 * The kernel AUTO caller returns between these programs; no test syscall. */
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>

#if defined(__mc68000__) && defined(TEST_IS_PRG)
/* Bounded scheduler fixture, including PD churn under simultaneous startup
 * TPAs. Native tracing proves the default64KiB CRT stack cannot fit the
 * legitimately available61KiB child TPA; these small roles need <1KiB. */
long _stksize = 16L * 1024;
#endif
#include <stdint.h>
#include <string.h>
#define LOG "C:\\GLOBAL.LOG"
#define MAGIC 0x713a2c01L
#ifndef GLOBAL_ROLE
#define GLOBAL_ROLE 0
#endif
static volatile int role=GLOBAL_ROLE; /* volatile keeps source/check image sizes equal */
static long data[8]; /* magic, creator PID, child PID, phase, old PD, gate, error, checker */
static void fail(long code)
{
    long fd=Fopen(LOG,2);
    if(fd>=0) { if(!data[6]) data[6]=code; (void)Fwrite(fd,sizeof(data),data); (void)Fclose(fd); }
    (void)Cconws("global cooperative scheduler fixture: FAIL ");
    (void)Cconout("0123456789abcdef"[(code>>4)&15]); (void)Cconout("0123456789abcdef"[code&15]); (void)Cconws("\r\n"); Pterm((short)code);
}
#define REQUIRE(c,n) do { if(!(c)) fail(n); } while(0)
static void read_log(void)
{
    long fd=Fopen(LOG,0), n;
    REQUIRE(fd>=0,1); n=Fread(fd,sizeof(data),data); (void)Fclose(fd);
    REQUIRE(n==sizeof(data) && data[0]==MAGIC && !data[6],2);
}
static void write_log(void)
{
    long fd=Fcreate(LOG,0), n;
    REQUIRE(fd>=0,3); n=Fwrite(fd,sizeof(data),data); (void)Fclose(fd); REQUIRE(n==sizeof(data),4);
}
int main(void)
{
    long pid,fd,loaded,result;
    unsigned long start;
    char tail[]={0,0};
    if(_base->p_cmdlin[0] && _base->p_cmdlin[1]=='w') { Pterm(23); return 99; }
    if(_base->p_cmdlin[0] && _base->p_cmdlin[1]=='n') { (void)Syield(); (void)Syield(); Pterm(19); return 99; }
    if(_base->p_cmdlin[0] && _base->p_cmdlin[1]=='g') {
        start=Ssystem(S_GETLVAL,0x4baL,0L);
        do { read_log(); if(!data[5]) (void)Syield(); REQUIRE(((unsigned long)Ssystem(S_GETLVAL,0x4baL,0L)-start)<1000UL,28); } while(!data[5]);
        Pterm(29); return 99;
    }
    if(role==3) {
        fd=Fcreate("C:\\GLOBAL0.ON",0); REQUIRE(fd>=0,5); (void)Fclose(fd); return 0;
    }
    if(role==1) {
        memset(data,0,sizeof(data)); data[0]=MAGIC; data[1]=Pgetpid(); data[3]=1;
        data[4]=(long)(uintptr_t)_base; write_log();
        pid=Pexec(100,"C:\\GLOBALCH.TOS",tail,0); REQUIRE(pid>0 && pid!=data[1],6);
        read_log(); REQUIRE(data[3]==1,7); data[2]=pid; write_log();
        (void)Cconws("global scheduler launcher: RETURNING with unexecuted child\r\n");
        return 0; /* child may not require this launcher or its PD/stack */
    }
    if(role==2) {
        read_log(); REQUIRE(data[3]==2 && data[1]!=Pgetpid(),8);
        /* No allocator-order assumption about this independent launcher. */
        data[7]=Pgetpid(); data[5]=1; write_log();
        start=Ssystem(S_GETLVAL,0x4baL,0L);
        do { (void)Syield(); read_log(); REQUIRE(((unsigned long)Ssystem(S_GETLVAL,0x4baL,0L)-start)<1000UL,10); } while(data[3]!=4);
        REQUIRE(Pwaitpid((short)data[2],1,0)==-33L,11); /* orphan is not this PROCESS's zombie */
        /* The existing authoritative wait handles both early and late completion. */
        tail[0]=1; tail[1]='w';
        loaded=Pexec(3,"C:\\GLOBALCH.TOS",tail,0); REQUIRE(loaded>0,12);
        pid=Pexec(106,0,(void *)(uintptr_t)loaded,0); REQUIRE(pid>0,13);
        (void)Syield(); result=Pwaitpid((short)pid,0,0);
        REQUIRE((unsigned long)result==((unsigned long)pid<<16|23) && Pwaitpid((short)pid,1,0)==-33L,14);
        pid=Pexec(100,"C:\\GLOBALCH.TOS",tail,0); REQUIRE(pid>0,15);
        result=Pwaitpid((short)pid,0,0);
        REQUIRE((unsigned long)result==((unsigned long)pid<<16|23) && Pwaitpid((short)pid,1,0)==-33L,16);
        /* Force a real basepage identity retirement/reuse under the same
         * loader policy, rather than assuming which hole a different image
         * will receive. The old orphan is never this replacement's child. */
        {
            BASEPAGE *first,*replacement;
            loaded=Pexec(3,"C:\\GLBSRC.TOS",tail,0); REQUIRE(loaded>0,26);
            first=(BASEPAGE *)(uintptr_t)loaded;
            REQUIRE(Mfree(first->p_env)==0 && Mfree(first)==0,24);
            loaded=Pexec(3,"C:\\GLBSRC.TOS",tail,0); REQUIRE(loaded>0,27);
            replacement=(BASEPAGE *)(uintptr_t)loaded;
            REQUIRE(first==replacement && Pwaitpid((short)data[2],1,0)==-33L,9);
            REQUIRE(Mfree(replacement->p_env)==0 && Mfree(replacement)==0,25);
        }
        fd=Fopen("C:\\GLOBAL0.ON",0); REQUIRE(fd>=0,17); (void)Fclose(fd);
        fd=Fcreate("C:\\GLOBALOK.ON",0); REQUIRE(fd>=0,18); (void)Fclose(fd);
        (void)Cconws("native public global launcher-return/later-entry/PD-reuse: PASS\r\n");
        return 0;
    }
    read_log(); REQUIRE(Pgetpid()==data[2] && Pgetppid()==0 && data[3]==1,19);
    data[3]=2; write_log();
    /* The first detached root entry must park with this real user THREAD
     * blocked in the existing PROCESS wait, and its child newly runnable. */
    tail[0]=1; tail[1]='g';
    pid=Pexec(100,"C:\\GLOBALCH.TOS",tail,0); REQUIRE(pid>0,29);
    result=Pwaitpid((short)pid,0,0);
    REQUIRE((unsigned long)result==((unsigned long)pid<<16|29),30);
    start=Ssystem(S_GETLVAL,0x4baL,0L);
    do { read_log(); if(!data[5]) (void)Syield(); REQUIRE(((unsigned long)Ssystem(S_GETLVAL,0x4baL,0L)-start)<1000UL,20); } while(!data[5]);
    data[3]=3; write_log();
    tail[0]=1; tail[1]='n';
    REQUIRE(Pexec(0,"C:\\GLOBALCH.TOS",tail,0)==19,21); /* nested launcher with global peers */
    read_log(); REQUIRE(Pgetppid()==0 && data[7]>0,22); data[3]=4; write_log();
    Pterm(37); return 99;
}
