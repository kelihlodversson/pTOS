/* Parallel GEM launches through standard headers and ordinary event messages. */
#include <stddef.h>
#include <gem.h>
#include "ptos.h"
#include "gem_launch.h"
#define VERIFY(c) do { if (!(c)) return 0x8a00 | __LINE__; } while (0)
static const char hex[] = "0123456789abcdef";
static uint32_t environment_hash(char **envp)
{
    uint32_t hash = 2166136261UL;
    unsigned i, j;
    for (i = 0; envp[i]; i++) {
        for (j = 0; envp[i][j]; j++) hash = (hash ^ (unsigned char)envp[i][j])*16777619UL;
        hash *= 16777619UL;
    }
    return hash;
}
static void encode(char *text, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 8; i++) text[i] = hex[(value >> (28-i*4)) & 15];
}
static uint32_t decode(const char *text)
{
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < 8; i++) value = (value << 4) | (text[i] <= '9' ? text[i]-'0' : text[i]-'a'+10);
    return value;
}
static short receive(short *message, unsigned long timeout, short *global)
{
    return mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
                         message,timeout,NULL,NULL,NULL,NULL,NULL,NULL,global);
}
static uint64_t process_call(uint32_t function, uint64_t a, uint64_t b, uint64_t c)
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | function;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory", "cc");
    return ax;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
int gem_launch_child(short *global, int argc, char **argv, char **envp)
{
    char command[260], tail[128];
    short message[8] = { 0x7e10,0,0,0,0,0,0,0 }, parent;
    unsigned i;
    VERIFY(argc == 2);
    parent = (short)decode(argv[1]+1);
    VERIFY(parent > 0 && environment_hash(envp) == decode(argv[1]+9));
    VERIFY(mt_shel_read(command,tail,global) == 1 && equal(command,"C:\\APPS\\GEMSHELL.TOS"));
    VERIFY((unsigned char)tail[0] == (argv[1][0] == 'm' ? 126 : 17));
    for (i = 0; i < 17; i++) VERIFY(tail[i+1] == argv[1][i]);
    for (i = 18; i <= (unsigned char)tail[0]; i++) VERIFY(tail[i] == ' ');
    message[1] = global[2];
    VERIFY(appl_write(parent,sizeof(message),message) == 1);
    /* The parent must keep running while the child is alive and waiting. */
    VERIFY(receive(message,2000,global) == MU_MESAG && message[0] == 0x7e11 && message[1] == parent);
    return 43;
}
int gem_launch_checks(short *global, char **envp)
{
    char command[] = "APPS\\GEMSHELL.TOS", tail[128] = { 0 }, saved[128];
    char cwd[128], after[128];
    unsigned char dta[64], saved_dta[64];
    uint64_t original_dta;
    short pid, message[8] = { 0 }, a, b, c, d;
    unsigned pass, i;
    /* Do not claim complete AES_SHELL support for a single launch submode. */
    VERIFY(appl_getinfo(AES_SHELL,&a,&b,&c,&d) == 0 && !a && !b && !c && !d);
    VERIFY(!shel_write(SWM_LAUNCH,GEMAPP,0,command,tail));
    VERIFY(!mt_shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,command,tail,NULL));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,"ABSENT.PRG",tail));
    VERIFY((int32_t)process_call(0x47,(uintptr_t)cwd,0,0) == 0);
    original_dta = process_call(0x2f,0,0,0);
    (void)process_call(0x1a,(uintptr_t)dta,0,0);
    VERIFY((int32_t)process_call(0x4e,(uintptr_t)"C:\\APPS\\*.DAT",0,0) == 0);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < sizeof(dta); i++) saved_dta[i] = dta[i];
        for (i = 0; i < sizeof(tail); i++) tail[i] = 0;
        tail[0] = pass ? 126 : 17;
        tail[1] = pass ? 'm' : 'l';
        encode(tail+2,(uint32_t)global[2]); encode(tail+10,environment_hash(envp));
        for (i = 18; i <= (unsigned char)tail[0]; i++) tail[i] = ' ';
        for (i = 0; i < sizeof(tail); i++) saved[i] = tail[i];
        VERIFY(wind_update(BEG_UPDATE) == 1);
        pid = pass ? mt_shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,command,tail,global) :
                     shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,command,tail);
        VERIFY(pid > 0 && pid != global[2] && wind_update(END_UPDATE) == 1 && !wind_update(END_UPDATE));
        VERIFY(equal(command,"APPS\\GEMSHELL.TOS"));
        for (i = 0; i < sizeof(tail); i++) VERIFY(saved[i] == tail[i]);
        VERIFY(process_call(0x2f,0,0,0) == (uintptr_t)dta);
        for (i = 0; i < sizeof(dta); i++) VERIFY(saved_dta[i] == dta[i]);
        VERIFY(receive(message,2000,global) == MU_MESAG && message[0] == 0x7e10 && message[1] == pid);
        message[0] = 0x7e11; message[1] = global[2];
        VERIFY(mt_appl_write(pid,sizeof(message),message,global) == 1);
        VERIFY((int32_t)process_call(0x13a,pid,0,0) == (int32_t)(((uint32_t)pid << 16) | 43));
        /* Unlike SWM_LAUNCH, SHW_PARALLEL sends no death-of-child message. */
        VERIFY(receive(message,0,global) == MU_TIMER);
        VERIFY((int32_t)process_call(0x4f,0,0,0) == (pass ? -49 : 0));
        VERIFY((int32_t)process_call(0x47,(uintptr_t)after,0,0) == 0 && equal(cwd,after));
    }
    (void)process_call(0x1a,original_dta,0,0);
    return 0;
}
