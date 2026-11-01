/* AES4 launch and CH_EXIT through the normal GEM source API. */
#include <mint/basepage.h>
#include <stddef.h>
#include <gem.h>
#include "ptos.h"
#include "cuis.h"
#include "gem_exit.h"
#define VERIFY(c) do { if (!(c)) return 0x8b00 | __LINE__; } while (0)
static int32_t process(uint32_t function, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | function;
    register uint64_t fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a),"S"(b),"d"(c),"r"(fourth)
        : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
static int32_t native_ui(uint32_t function, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    register uint64_t ax __asm__("rax") = ((uint64_t)CUIS_SERVICE_CLASS << 32) | function;
    register uint64_t fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a),"S"(b),"d"(c),"r"(fourth)
        : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
static short receive(short *message, unsigned long timeout, short *global)
{
    return mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
                         message,timeout,NULL,NULL,NULL,NULL,NULL,NULL,global);
}
static int exit_message(short *message, short pid, short code)
{
    return message[0] == CH_EXIT && !message[1] && !message[2] && message[3] == pid &&
           message[4] == code && !message[5] && !message[6] && !message[7];
}
static int synchronous_exit_checks(short *global)
{
    char tail[3] = { 1,'n',0 };
    int32_t session, token, image, result;
    uint32_t environment;
    short message[8], pid;
    unsigned mode;
    session = native_ui(CUIS_SESSION_START,0,0,0,0);
    VERIFY(session >= 0);
    for (mode = 4; mode <= 6; mode += 2) {
        token = native_ui(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
        VERIFY(token > 0);
        image = process(0x4b,3,(uintptr_t)"APPS\\GEMCHILD.PRG",(uintptr_t)tail,0);
        VERIFY(image > 0);
        environment = (uint32_t)(uintptr_t)((const BASEPAGE *)(uintptr_t)(uint32_t)image)->p_env;
        VERIFY(native_ui(CUIS_CHILD_WATCH,session,CUIS_CHILD_BIND,token,(uint32_t)image) == 0);
        VERIFY(process(0x4b,mode,0,(uint32_t)image,0) == 0x1234);
        if (mode == 4) {
            VERIFY(process(0x49,(uint32_t)image,0,0,0) == 0);
            VERIFY(process(0x49,environment,0,0,0) == 0);
        }
        result = native_ui(CUIS_CHILD_RESULT,session,0,0,0);
        VERIFY(result > 0 && (result & 0xffff) == 0x1234);
        pid = (short)((uint32_t)result >> 16);
        VERIFY(pid > 0 && pid != global[2]);
        VERIFY(mt_appl_read(-1,sizeof(message),message,global) == 1 && exit_message(message,pid,0x1234));
        VERIFY(native_ui(CUIS_CHILD_RESULT,session,0,0,0) == 0);
        VERIFY(native_ui(CUIS_CHILD_WATCH,session,CUIS_CHILD_CANCEL,token,0) == -36);
        VERIFY(receive(message,0,global) == MU_TIMER);
    }
    return 0;
}
int gem_exit_checks(short *global)
{
    char tail[3] = { 1,'n',0 };
    short message[8], pid, earlier, a, b, c, d;
    unsigned i;
    static const char roles[] = { 'n','e','f' };
    static const short results[] = { 0x1234,0x5678,0x0400 };
    VERIFY(appl_getinfo(AES_MESSAGE,&a,&b,&c,&d) == 1 && (a & 0x20) && !b && !c && !d);
    VERIFY(!shel_write(SWM_LAUNCH,0,CL_PARSE,"APPS\\GEMCHILD.PRG",tail));
    VERIFY(!shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\GEMSHELL.TOS",tail));
    for (i = 0; i < 3; i++) {
        tail[1] = roles[i];
        /* Mode 0 infers GEM from the extension; wisgr is ignored. */
        pid = i ? mt_shel_write(SWM_LAUNCH,99,CL_NORMAL,"APPS\\GEMCHILD.APP",tail,global) :
                  shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\GEMCHILD.PRG",tail);
        VERIFY(pid > 0);
        VERIFY((receive(message,2000,global) & MU_MESAG) && exit_message(message,pid,results[i]));
        VERIFY(process(0x13a,pid,1,0,0) == -33); /* Only the notified child was reaped. */
        VERIFY(receive(message,0,global) == MU_TIMER); /* Exactly one notification. */
    }
    tail[1] = 'n';
    /* An ordinary Pexec child is not observed or reaped by libaes. */
    pid = (short)process(0x4b,100,(uintptr_t)"APPS\\GEMCHILD.PRG",(uintptr_t)tail,0);
    VERIFY(pid > 0);
    VERIFY(receive(message,20,global) == MU_TIMER);
    VERIFY(process(0x13a,pid,0,0,0) == (int32_t)(((uint32_t)pid << 16) | 0x34));
    /* Explicitly collecting a shell child retains its full-word GEM notice. */
    pid = shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\GEMCHILD.PRG",tail);
    VERIFY(pid > 0 && process(0x13a,pid,0,0,0) == (int32_t)(((uint32_t)pid << 16) | 0x34));
    VERIFY(mt_appl_read(-1,sizeof(message),message,global) == 1 && exit_message(message,pid,0x1234));
    VERIFY(receive(message,0,global) == MU_TIMER);
    /* A fixed ET_EXEC without relocation tables must keep its link address. */
    pid = shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\FIXED.PRG",tail);
    VERIFY(pid > 0 && (receive(message,2000,global) & MU_MESAG) && exit_message(message,pid,0x2468));
    /* Completion/reaping of A before launching B must preserve exit FIFO. */
    tail[1] = 'e';
    earlier = shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\GEMCHILD.PRG",tail);
    VERIFY(earlier > 0 && process(0x13a,earlier,0,0,0) == (int32_t)(((uint32_t)earlier << 16) | 0x78));
    tail[1] = 'n';
    pid = shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\GEMCHILD.PRG",tail);
    VERIFY(pid > 0 && process(0x13a,pid,0,0,0) == (int32_t)(((uint32_t)pid << 16) | 0x34));
    VERIFY(mt_appl_read(-1,sizeof(message),message,global) == 1 && exit_message(message,earlier,0x5678));
    VERIFY(mt_appl_read(-1,sizeof(message),message,global) == 1 && exit_message(message,pid,0x1234));
    VERIFY(receive(message,0,global) == MU_TIMER);
    VERIFY(synchronous_exit_checks(global) == 0);
    return 0;
}
