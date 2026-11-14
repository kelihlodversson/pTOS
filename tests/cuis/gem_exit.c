/* AES4 launch and CH_EXIT through the normal GEM source API. */
#include <mint/basepage.h>
#include <stddef.h>
#include <gem.h>
#include "ptos.h"
#include "cuis.h"
#include "gem_exit.h"
#include "autoconf.h"
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
#define ORDER_READY 0x7e30
#define ORDER_GO 0x7e31
int gem_exit_order_child(int argc, char **argv)
{
    short parent = 0, id, message[8] = { ORDER_READY,0,0,0,0,0,0,0 };
    unsigned i, number = 0;
    VERIFY(argc == 3 && argv[1][0] == 'o' && !argv[1][1]);
    for (i = 0; argv[2][i]; i++) {
        VERIFY(argv[2][i] >= '0' && argv[2][i] <= '9' && number <= 3276);
        number = number*10 + argv[2][i]-'0';
    }
    VERIFY(number && number <= 32767); parent = (short)number;
    id = appl_init(); VERIFY(id > 0);
    message[1] = message[3] = id;
    VERIFY(appl_write(parent,sizeof(message),message));
    VERIFY(evnt_mesag(message) && message[0] == ORDER_GO && message[1] == parent);
    VERIFY(appl_exit());
    return 0x3456;
}
static int ordered_exit_checks(short *global)
{
    char tail[16] = { 0,'o',' ' }, reverse[5], fast_tail[3] = { 1,'e',0 };
    unsigned number = (unsigned short)global[2], used = 3, digits = 0;
    short first, second, peer, message[8], go[8] = { ORDER_GO,0,0,0,0,0,0,0 };
    do { reverse[digits++] = '0'+number%10; number /= 10; } while (number);
    while (digits) tail[used++] = reverse[--digits];
    tail[0] = used-1;
    first = mt_shel_write(SWM_LAUNCH,GEMAPP,CL_NORMAL,"APPS\\GEMCHILD.PRG",tail,global);
    VERIFY(first > 0 && receive(message,2000,global) == MU_MESAG && message[0] == ORDER_READY);
    peer = message[3];
    VERIFY(peer > 0 && message[1] == peer &&
           mt_appl_find((const char *)(uintptr_t)(0xfffe0000UL | (unsigned short)peer),global) == first);
    second = mt_shel_write(SWM_LAUNCH,GEMAPP,CL_NORMAL,"APPS\\GEMCHILD.PRG",fast_tail,global);
    VERIFY(second > 0 && second != first);
    /* Hold the first child alive until the second has completed. No timer
     * or scheduler ordering assumption decides which PROCESS exits first. */
    VERIFY(process(0x13a,second,0,0,0) == (int32_t)(((uint32_t)second << 16) | 0x78));
    go[1] = global[2];
    VERIFY(mt_appl_write(peer,sizeof(go),go,global));
    VERIFY(process(0x13a,first,0,0,0) == (int32_t)(((uint32_t)first << 16) | 0x56));
    /* Both public completions are consumed before either notification read. */
    VERIFY(mt_appl_read(APR_NOWAIT,sizeof(message),message,global) && exit_message(message,second,0x5678));
    VERIFY(mt_appl_read(APR_NOWAIT,sizeof(message),message,global) && exit_message(message,first,0x3456));
    VERIFY(receive(message,0,global) == MU_TIMER);
    VERIFY(process(0x13a,first,1,0,0) == -33 && process(0x13a,second,1,0,0) == -33);
    {
        static const char marker[] = "x32 GEM reverse completion order and independent reaping: PASS\r\n";
        (void)ptos_write(1,marker,sizeof(marker)-1);
    }
    return 0;
}
static int synchronous_exit_checks(short *global)
{
    char tail[3] = { 1,'n',0 };
    int32_t session, token, image, result;
#if CONF_WITH_SHARED_VM_PEXEC_TEST
    uint32_t environment=0;
#endif
    short message[8], pid;
    unsigned mode, variant;
    static const char roles[] = { 'n','f','t' };
    static const short codes[] = { 0x1234,0x0400,-1 };
    session = native_ui(CUIS_SESSION_START,0,0,0,0);
    VERIFY(session >= 0);
    for (mode = 4; mode <=
#if CONF_WITH_SHARED_VM_PEXEC_TEST
         104
#else
         6
#endif
         ; mode = mode == 4 ? 6 : mode == 6 ? 104 : 106) {
        for (variant = 0; variant < sizeof(roles); variant++) {
            tail[1] = roles[variant];
            token = native_ui(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
            VERIFY(token > 0);
            image = process(0x4b,3,(uintptr_t)
#if CONF_WITH_SHARED_VM_PEXEC_TEST
                (mode==6?"APPS\\GEMCHILD.PRG":"APPS\\GEMPIE.TOS"),
#else
                "APPS\\GEMCHILD.PRG",
#endif
                (uintptr_t)tail,0);
            VERIFY(image > 0);
#if CONF_WITH_SHARED_VM_PEXEC_TEST
            if(mode!=6) environment=(uintptr_t)((BASEPAGE *)(uintptr_t)image)->p_env;
#endif
            VERIFY(native_ui(CUIS_CHILD_WATCH,session,CUIS_CHILD_BIND,token,(uint32_t)image) == 0);
#if CONF_WITH_SHARED_VM_PEXEC_TEST
            result=process(0x4b,mode,0,(uint32_t)image,0);
            if(mode==104) {
                int32_t waited;
                VERIFY(result>0);
                waited=process(0x13a,result,0,0,0);
                VERIFY((uint32_t)waited>>16==(uint32_t)result &&
                    (waited&65535)==(variant==1?0x400:variant==2?255:0x34));
                VERIFY(process(0x13a,result,1,0,0)==-33);
            } else VERIFY(result==(variant?(mode==4 && variant==1?0x400:0xffff):0x1234));
#else
            if (mode == 4)
                VERIFY(process(0x4b,4,0,(uint32_t)image,0) == -32);
            VERIFY(process(0x4b,6,0,(uint32_t)image,0) == (variant ? 0xffff : 0x1234));
#endif
            result = native_ui(CUIS_CHILD_RESULT,session,0,0,0);
            VERIFY(result > 0 && (result & 0xffff) == (uint16_t)codes[variant]);
            pid = (short)((uint32_t)result >> 16);
            VERIFY(pid > 0 && pid != process(0x10b,0,0,0,0));
            VERIFY(mt_appl_read(-1,sizeof(message),message,global) == 1 && exit_message(message,pid,codes[variant]));
            VERIFY(native_ui(CUIS_CHILD_RESULT,session,0,0,0) == 0);
            VERIFY(native_ui(CUIS_CHILD_WATCH,session,CUIS_CHILD_CANCEL,token,0) == -36);
            VERIFY(receive(message,0,global) == MU_TIMER);
#if CONF_WITH_SHARED_VM_PEXEC_TEST
            if(mode!=6) {
                VERIFY(process(0x49,(uint32_t)image,0,0,0)==0);
                VERIFY(process(0x49,environment,0,0,0)==0);
            }
#endif
        }
    }
#if CONF_WITH_SHARED_VM_PEXEC_TEST
    {
        static const char text[]="x32 shared-Pexec full result, canonical reap and independent CH_EXIT: PASS\r\n";
        (void)process(0x40,1,sizeof(text)-1,(uintptr_t)text,0);
    }
#endif
    return 0;
}

int gem_exit_checks(short *global)
{
    char tail[3] = { 1,'n',0 };
    short message[8], pid, earlier, a, b, c, d;
    unsigned i;
    static const char roles[] = { 'n','e','f','t' };
    static const short results[] = { 0x1234,0x5678,0x0400,-1 };
    static const unsigned short waited[] = { 0x34,0x78,0x0400,0xff };
    VERIFY(appl_getinfo(AES_MESSAGE,&a,&b,&c,&d) == 1 && (a & 0x20) && !b && !c && !d);
    VERIFY(!shel_write(SWM_LAUNCH,0,2,"APPS\\GEMCHILD.PRG",tail));
    VERIFY(!shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\ABSENT.TOS",tail));
    for (i = 0; i < sizeof(roles); i++) {
        tail[1] = roles[i];
        /* Mode 0 infers GEM from the extension; wisgr is ignored. */
        pid = i ? mt_shel_write(SWM_LAUNCH,99,CL_NORMAL,"APPS\\GEMCHILD.APP",tail,global) :
                  shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\GEMCHILD.PRG",tail);
        VERIFY(pid > 0 && pid != process(0x10b,0,0,0,0));
        VERIFY((receive(message,2000,global) & MU_MESAG) && exit_message(message,pid,results[i]));
        /* CH_EXIT does not consume the independent public PROCESS zombie. */
        VERIFY(process(0x13a,pid,1,0,0) == (int32_t)(((uint32_t)pid << 16) | waited[i]));
        VERIFY(process(0x13a,pid,1,0,0) == -33); /* One public reap. */
        VERIFY(receive(message,0,global) == MU_TIMER); /* Exactly one notification. */
    }
    tail[1] = 'n';
    /* An ordinary Pexec child is not observed or reaped by libaes. */
    pid = (short)process(0x4b,100,(uintptr_t)"APPS\\GEMCHILD.PRG",(uintptr_t)tail,0);
    VERIFY(pid > 0 && pid != process(0x10b,0,0,0,0));
    VERIFY(receive(message,20,global) == MU_TIMER);
    VERIFY(process(0x13a,pid,0,0,0) == (int32_t)(((uint32_t)pid << 16) | 0x34));
    /* The opposite order retains full normal words and fault encoding too. */
    for (i = 0; i < sizeof(roles); i++) {
        tail[1] = roles[i];
        pid = shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\GEMCHILD.PRG",tail);
        VERIFY(pid > 0 && process(0x13a,pid,0,0,0) == (int32_t)(((uint32_t)pid << 16) | waited[i]));
        VERIFY(process(0x13a,pid,1,0,0) == -33);
        VERIFY(mt_appl_read(-1,sizeof(message),message,global) == 1 && exit_message(message,pid,results[i]));
        VERIFY(receive(message,0,global) == MU_TIMER);
    }
    tail[1] = 'n';
    /* A fixed ET_EXEC without relocation tables must keep its link address. */
    pid = shel_write(SWM_LAUNCH,0,CL_NORMAL,"APPS\\FIXED.PRG",tail);
    VERIFY(pid > 0 && (receive(message,2000,global) & MU_MESAG) && exit_message(message,pid,0x2468));
    VERIFY(process(0x13a,pid,1,0,0) == (int32_t)(((uint32_t)pid << 16) | 0x68));
    /* The same fixed executable can run without an AES session as text. */
    pid = shel_write(SWM_LAUNCH,TOSAPP,CL_NORMAL,"APPS\\FIXED.TOS",tail);
    VERIFY(pid > 0 && (receive(message,2000,global) & MU_MESAG) && exit_message(message,pid,0x2468));
    VERIFY(process(0x13a,pid,1,0,0) == (int32_t)(((uint32_t)pid << 16) | 0x68));
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
    VERIFY(ordered_exit_checks(global) == 0);
    return 0;
}
