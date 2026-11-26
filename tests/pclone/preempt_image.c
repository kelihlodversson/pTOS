/* A small independently loaded PROCESS for authentic timer GPR/FPU proof.
 * Its root is created by the loader, including on ROM-window MMU backends
 * whose existing private-clone operation deliberately returns EINVFN. */
#include "../entry_args.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include "../../include/pclone.h"
#include "trap.h"
#if defined(__mc68000__)
long _stksize=8192;
extern long clone_preempt_registers(long),clone_preempt_registers_alt(long);
#else
extern long clone_preempt_registers(void),clone_preempt_registers_alt(void);
#endif
int main(int argc, char **argv)
{
    unsigned role=test_entry_argument(argc, argv)[0] == '1',attempt=0,cycle;
    long report[3],fd,error=0;
#if defined(__mc68000__)
    long fpu=0;
    (void)Ssystem(S_GETCOOKIE,0x5f465055L,&fpu);
#endif
    report[0]=pclone_trap(0,0xc8920100UL);report[1]=Pgetpid();
#if defined(__mc68000__)
    report[2]=fpu;
#else
    report[2]=1; /* these probes unconditionally execute their FPU patterns */
#endif
    fd=Fcreate(role?"PREEMPR1.DAT":"PREEMPR0.DAT",0);
    if(fd<0)return 81;
    if(Fwrite((short)fd,sizeof(report),report)!=sizeof(report))error=1;
    (void)Fclose((short)fd);
    if(error)return 82;
    do {
        fd=Fopen("PREEMPG.ON",0);
        if(fd<0)(void)Syield(); /* outside the register observation window */
    }while(fd<0 && ++attempt<1024);
    if(fd<0)return 84;
    (void)Fclose((short)fd);
    for(cycle=0;cycle<4;cycle++) {
#if defined(__mc68000__)
        error|=role?clone_preempt_registers_alt(fpu):clone_preempt_registers(fpu);
#else
        error|=role?clone_preempt_registers_alt():clone_preempt_registers();
#endif
    }
    return error?80+error:0;
}
