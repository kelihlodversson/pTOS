/* Standard GEM modes, console restoration and observed/unobserved lifetimes. */
#include <gem.h>
#include "ptos.h"
#include "gem_explicit.h"
#define VERIFY(c) do { if (!(c)) return 0x6b00 | __LINE__; } while (0)
static int32_t waitpid(short pid)
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | 0x13a;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"((uint64_t)pid), "S"(0ULL), "d"(0ULL)
                      : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
static int32_t directory_call(unsigned fn, uintptr_t a, unsigned b)
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | fn;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"((uint64_t)a),"S"((uint64_t)b)
                      : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
int gem_explicit_checks(short *global)
{
    short pid, message[8], mode, kind, result;
    char tail[128] = { 3,0,0,0,0 }, original[128], current[128], command[260], read_tail[128];
    static char environment[] = "DIRECTORY_TEST=present\0\0";
    SHELW extended = { "C:\\APPS\\EXPLICIT.PRG",0,0,NULL,NULL,0,0 };
    int32_t drive = directory_call(0x19,0,0);
    VERIFY(drive >= 0 && drive < 26 && directory_call(0x47,(uintptr_t)original,3) == 0);
    VERIFY(directory_call(0xe,2,0) >= 0 && directory_call(0x3b,(uintptr_t)"C:\\GEMSYS",0) == 0);
    unsigned pass;
    for (pass = 0; pass < 9; pass++) {
        mode = pass < 2 || pass == 4 || pass == 7 || pass == 8 ? SHW_IMMED : SHW_PARALLEL;
        kind = (pass && pass < 4) || pass == 5 ? TOSAPP : GEMAPP;
        tail[1] = pass == 3 ? 'f' : kind == TOSAPP ? 't' : 'g';
        tail[2] = pass == 4 || pass == 8 ? 'l' : pass == 2 || pass == 3 ? 'i' : 'a';
        tail[3] = pass == 8 ? 'e' : 'n';
        if (pass < 4) {
            pid = pass & 1 ? mt_shel_write(SWM_LAUNCHNOW,kind,mode,"C:\\APPS\\EXPLICIT.PRG",tail,global) :
                            shel_write(SWM_LAUNCHNOW,kind,mode,pass ? "C:\\APPS\\EXPLICIT.PRG" : "C:\\APPS\\EXPLICIT.TOS",tail);
        } else {
            extended.defdir = pass == 4 || pass == 8 ? "LIB" : pass == 5 ? "C:\\APPS" : NULL;
            extended.env = pass == 8 ? environment : (char *)1;
            pid = mt_shel_write(SWM_LAUNCHNOW|SW_DEFDIR|(pass == 8 ? SW_ENVIRON : 0),kind,mode,&extended,tail,global);
        }
        VERIFY(directory_call(0x19,0,0) == 2 && directory_call(0x47,(uintptr_t)current,3) == 0);
        VERIFY(equal(current,"\\GEMSYS"));
        VERIFY(pid > 0 && pid != global[2]);
        /* The console lease exists before the child gets its first timeslice. */
        result = wind_update(BEG_UPDATE|NO_BLOCK);
        VERIFY(result == (kind == TOSAPP ? 0 : 1));
        if (result) VERIFY(wind_update(END_UPDATE));
        if (mode == SHW_PARALLEL) {
            VERIFY(waitpid(pid) == (int32_t)(((uint32_t)pid << 16) | (pass == 3 ? 0x0400 : 37)));
        } else {
            VERIFY(mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,3000,
                                 0,0,0,0,0,0,global) == MU_MESAG);
            VERIFY(message[0] == CH_EXIT && !message[1] && !message[2] && message[3] == pid && message[4] == 37);
        }
        VERIFY(wind_update(BEG_UPDATE|NO_BLOCK) && wind_update(END_UPDATE));
        VERIFY(evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,0,
                          0,0,0,0,0,0) == MU_TIMER);
        VERIFY(tail[0] == 3 && !tail[4]);
    }
    extended.defdir = "C:\\MISSING"; extended.env = (char *)1;
    VERIFY(!shel_write(SWM_LAUNCHNOW|SW_DEFDIR,GEMAPP,SHW_IMMED,&extended,tail));
    VERIFY(!shel_write(SWM_LAUNCHNOW|SW_DEFDIR,TOSAPP,SHW_PARALLEL,&extended,tail));
    VERIFY(wind_update(BEG_UPDATE|NO_BLOCK) && wind_update(END_UPDATE));
    VERIFY(directory_call(0x47,(uintptr_t)current,3) == 0 && equal(current,"\\GEMSYS"));
    VERIFY(shel_read(command,read_tail)); /* Startup metadata is still usable. */
    VERIFY(directory_call(0x3b,(uintptr_t)(*original ? original : "\\"),0) == 0 && directory_call(0xe,drive,0) >= 0);
    {
        static const char marker[] = "x32 GEM shell launch directories: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    {
        static const char marker[] = "x32 GEM explicit immediate/parallel launches: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    return 0;
}
