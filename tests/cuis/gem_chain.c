/* Classic GEM handoff over isolated native processes and standard GEM calls. */
#include <stddef.h>
#include <gem.h>
#include "cuis.h"
#include "ptos.h"
#include "gem_chain.h"
static int failure(unsigned line)
{
    const char *digits = "0123456789abcdef";
    char text[] = "x32 GEM chain failure: 0000\r\n";
    unsigned i;
    for (i = 0; i < 4; i++) text[23+i] = digits[(line >> (12-i*4)) & 15];
    ptos_write(2,text,sizeof(text)-1);
    return 0x8d00 | line;
}
#define VERIFY(c) do { if (!(c)) return failure(__LINE__); } while (0)
static const char digits[] = "0123456789abcdef";
static int32_t call(uint32_t cls, uint32_t fn, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    register uint64_t ax __asm__("rax") = ((uint64_t)cls << 32) | fn;
    register uint64_t fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a),"S"(b),"d"(c),"r"(fourth)
        : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
static void encode(char *out, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 8; i++) out[i] = digits[(value >> (28-i*4)) & 15];
}
static uint32_t decode(const char *in)
{
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < 8; i++) value = (value << 4) | (in[i] <= '9' ? in[i]-'0' : in[i]-'a'+10);
    return value;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static short receive(short *message, unsigned long timeout, short *global)
{
    return mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
                        message,timeout,NULL,NULL,NULL,NULL,NULL,NULL,global);
}
int gem_chain_client(int argc, char **argv)
{
    char role, target[128] = { 25 }, state[5] = { 0 }, directory[128], *value;
    short parent, me = gl_apid, message[8] = { 0x7d10,0,0,0,0,0,0,0 };
    int32_t session, output = -1;
    const char *program;
    VERIFY(argc == 2);
    role = argv[1][0]; parent = (short)decode(argv[1]+1);
    VERIFY(parent > 0 && me > 0);
    if (role == 'h') {
        VERIFY(receive(message,5000,aes_global) & MU_MESAG);
        return 0;
    }
    if (role == 't' || role == 'u' || role == 'v' || role == 'w' || role == 'k') {
        VERIFY(shel_envrn(&value,"CHAIN_ENV=") == 1 && value && equal(value,"original"));
        VERIFY(call(1,0x47,(uintptr_t)directory,0,0,0) == 0 && equal(directory,role == 'k' ? "\\WORK" : "\\APPS"));
        VERIFY(call(1,0x10c,0,0,0,0) == (int32_t)decode(argv[1]+17)); /* Stable controller. */
        if (role == 'v' || role == 'w') {
            VERIFY(call(1,0x40,1,5,(uintptr_t)"CHAIN",0) == 5);
            if (role == 'w') VERIFY(call(1,0x40,2,3,(uintptr_t)"ERR",0) == 3);
        }
        VERIFY(shel_put(role == 'u' ? "WRNG" : "DONE",4) == 1);
        message[0] = role == 'u' ? 0x7d99 : 0x7d12;
        message[1] = me; message[3] = (short)decode(argv[1]+9);
        VERIFY(appl_write(parent,sizeof(message),message) == 1);
        return 0;
    }
    VERIFY(call(1,0x3b,(uintptr_t)"C:\\APPS",0,0,0) == 0);
    VERIFY(shel_put("PEND",4) == 1);
    if (role == 'o' || role == 'p' || role == 'q') {
        output = call(1,0x3c,(uintptr_t)"C:\\WORK\\CHAIN.OUT",0,0,0);
        VERIFY(output >= 6 && call(1,0x46,1,output,0,0) == 0);
        if (role == 'q') VERIFY(call(1,0x46,2,output,0,0) == 0);
    }
    target[1] = role == 'r' ? 'u' : 't';
    if (output >= 0) target[1] = role == 'q' ? 'w' : 'v';
    encode(target+2,(uint32_t)parent); encode(target+10,(uint32_t)me);
    encode(target+18,decode(argv[1]+9));
    program = role == 'j' ? "DELETED.PRG" : "CHAIN.PRG";
    if (role == 'z' || role == 'n') {
        SHELW extended = { "C:\\APPS\\CHAIN.PRG",0,0,role == 'z' ? "C:\\WORK" : NULL,(char *)1,0,0 };
        if (role == 'z') target[1] = 'k';
        else VERIFY(call(1,0x3b,(uintptr_t)"C:\\WORK",0,0,0) == 0);
        VERIFY(shel_write(SWM_LAUNCHNOW|SW_DEFDIR,GEMAPP,SHW_CHAIN,&extended,target) == 1);
        extended.defdir = "C:\\MISSING";
        VERIFY(!shel_write(SWM_LAUNCHNOW|SW_DEFDIR,GEMAPP,SHW_CHAIN,&extended,target));
    } else VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_CHAIN,program,target) == 1);
    if (role == 'o' || role == 'q') VERIFY(call(1,0x3e,output,0,0,0) == 0);
    if (role == 'r') {
        target[1] = 't';
        VERIFY(mt_shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_CHAIN,program,target,aes_global) == 1);
    }
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_CHAIN,"ABSENT.PRG",target));
    if (role == 'c') {
        session = call(CUIS_SERVICE_CLASS,CUIS_SESSION_START,0,0,0,0);
        VERIFY(session >= 0 && call(CUIS_SERVICE_CLASS,CUIS_PROCESS_DEFER,session,0,0,0) == 0);
        VERIFY(call(CUIS_SERVICE_CLASS,CUIS_PROCESS_DEFER,session,0,0,0) == 0);
    }
    if (role == 'j') VERIFY(call(1,0x41,(uintptr_t)"C:\\APPS\\DELETED.PRG",0,0,0) == 0);
    VERIFY(shel_envrn(&value,"CHAIN_ENV=") == 1 && value && equal(value,"original"));
    *value = 'M'; /* The successor must keep the already copied environment. */
    VERIFY(call(1,0x3b,(uintptr_t)"C:\\WORK",0,0,0) == 0);
    VERIFY(evnt_timer(20) == 1 && shel_get(state,4) == 1 && equal(state,"PEND"));
    VERIFY(appl_exit() == 1 && call(1,0xff,0,0,0,0) == 0 && appl_init() == me);
    VERIFY(shel_get(state,4) == 1 && equal(state,"PEND"));
    message[1] = me; message[3] = role;
    VERIFY(appl_write(parent,sizeof(message),message) == 1);
    if (role != 'd') VERIFY(receive(message,5000,aes_global) & MU_MESAG);
    /* Replace the staged handoff while helpers occupy the initial arena.
     * The second snapshot uses the current WORK directory; restore its input
     * environment only for preparation, then mutate it again before exit. */
    if (role == 'e') {
        target[1] = 'k';
        *value = 'o';
        VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_CHAIN,program,target) == 1);
        *value = 'M';
    }
    if (role == 'f') __asm__ volatile ("ud2");
    if (role == 'x') _Exit(41);
    return 37;
}
int gem_chain_checks(short *global)
{
    static const char roles[] = { 's','r','c','f','x','d','j','e','o','p','q','z','n' };
    static const char environment[] = "CHAIN_ENV=original\0\0";
    char tail[128] = { 17 }, contents[9];
    short source, message[8], helpers[5];
    unsigned i, j;
    int32_t status;
    for (i = 0; i < sizeof(roles); i++) {
        tail[1] = roles[i]; encode(tail+2,(uint32_t)global[2]);
        encode(tail+10,(uint32_t)call(1,0x10c,0,0,0,0));
        status = call(1,0x4b,roles[i] == 'd' ? 0 : 100,(uintptr_t)"APPS\\CHAIN.PRG",
                      (uintptr_t)tail,(uintptr_t)environment);
        if (roles[i] == 'd') {
            VERIFY(status == 37);
            VERIFY(receive(message,2000,global) & MU_MESAG);
            VERIFY(message[0] == 0x7d10 && message[3] == roles[i]);
            source = message[1];
        } else {
            VERIFY(status > 0); source = (short)status;
            VERIFY(receive(message,2000,global) & MU_MESAG);
            VERIFY(message[0] == 0x7d10 && message[1] == source && message[3] == roles[i]);
            VERIFY(receive(message,0,global) == MU_TIMER); /* Source is still alive. */
            if (roles[i] == 'e') {
                tail[1] = 'h';
                for (j = 0; j < 5; j++) {
                    status = call(1,0x4b,100,(uintptr_t)"APPS\\CHAIN.PRG",(uintptr_t)tail,(uintptr_t)environment);
                    VERIFY(status > 0); helpers[j] = (short)status;
                }
            }
            message[0] = 0x7d11; message[1] = global[2];
            VERIFY(appl_write(source,sizeof(message),message) == 1);
            status = call(1,0x13a,source,0,0,0);
            VERIFY(status == (int32_t)(((uint32_t)source << 16) | (roles[i] == 'f' ? 0x0400 : roles[i] == 'x' ? 41 : 37)));
        }
        if (roles[i] == 'c') VERIFY(receive(message,20,global) == MU_TIMER);
        else {
            VERIFY(receive(message,2000,global) & MU_MESAG);
            VERIFY(message[0] == 0x7d12 && message[3] == source && message[1] != source);
        }
        if (roles[i] == 'e') {
            VERIFY(evnt_timer(20) == 1);
            for (j = 0; j < 5; j++) {
                message[0] = 0x7d11; message[1] = global[2];
                VERIFY(appl_write(helpers[j],sizeof(message),message) == 1);
                VERIFY(call(1,0x13a,helpers[j],0,0,0) == (int32_t)((uint32_t)helpers[j] << 16));
            }
        }
        VERIFY(receive(message,0,global) == MU_TIMER);
        if (roles[i] == 'o' || roles[i] == 'p' || roles[i] == 'q') {
            status = call(1,0x3d,(uintptr_t)"C:\\WORK\\CHAIN.OUT",0,0,0);
            VERIFY(status >= 6);
            VERIFY(call(1,0x3f,status,sizeof(contents),(uintptr_t)contents,0) == (roles[i] == 'q' ? 8 : 5));
            contents[roles[i] == 'q' ? 8 : 5] = 0;
            VERIFY(equal(contents,roles[i] == 'q' ? "CHAINERR" : "CHAIN"));
            VERIFY(call(1,0x3e,status,0,0,0) == 0);
            VERIFY(call(1,0x41,(uintptr_t)"C:\\WORK\\CHAIN.OUT",0,0,0) == 0);
        }
    }
    return 0;
}
