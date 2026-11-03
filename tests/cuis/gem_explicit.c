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
int gem_explicit_checks(short *global)
{
    short pid, message[8], mode, kind, result;
    char tail[128] = { 1,0,0 };
    unsigned pass;
    for (pass = 0; pass < 4; pass++) {
        mode = pass < 2 ? SHW_IMMED : SHW_PARALLEL;
        kind = pass ? TOSAPP : GEMAPP;
        tail[1] = pass == 3 ? 'f' : pass ? 't' : 'g';
        pid = pass & 1 ? mt_shel_write(SWM_LAUNCHNOW,kind,mode,"C:\\APPS\\EXPLICIT.PRG",tail,global) :
                        shel_write(SWM_LAUNCHNOW,kind,mode,pass ? "C:\\APPS\\EXPLICIT.PRG" : "C:\\APPS\\EXPLICIT.TOS",tail);
        VERIFY(pid > 0 && pid != global[2]);
        /* The console lease exists before the child gets its first timeslice. */
        result = wind_update(BEG_UPDATE|NO_BLOCK);
        VERIFY(result == (pass ? 0 : 1));
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
        VERIFY(tail[0] == 1 && !tail[2]);
    }
    {
        static const char marker[] = "x32 GEM explicit immediate/parallel launches: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    return 0;
}
