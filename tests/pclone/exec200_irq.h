/* Run an authentic retained return on a heap-backed USER stack. */
#ifndef TEST_EXEC200_IRQ_H
#define TEST_EXEC200_IRQ_H
#include "irq_restart_probe.h"
#include "../../include/threadcalls.h"

#define EXEC200_IRQ_STACK 32768
#define EXEC200_IRQ_GATE "GOIRQ.ON"
extern char clone_irq_registers_end[];
#if defined(__mc68000__)
extern long clone_irq_registers(long fpu);
#else
extern long clone_irq_registers(void);
#endif

static long exec200_irq_thread(void (*entry)(void *), void *stack)
{
    struct ptos_clone_args args={0};
    args.flags=CLONE_VM|CLONE_SAME_PROCESS;
    args.entry=entry;
    args.stack_base=stack;
    args.stack_size=EXEC200_IRQ_STACK;
    return exec200_probe(&args,sizeof(args));
}

static void exec200_irq_join(long tid, void *stack)
{
    int32_t status=-1;
    long result=trap_1_wll(GEMDOS_PTHJOIN,tid,(long)&status);
    if(result || status)exec200_fail("IRQ", "join peer", result?result:status);
    if(Mfree(stack))exec200_fail("IRQ", "peer stack release", 0);
}

static void exec200_irq_peer(void *unused)
{
    (void)unused;
    Pterm(Pgetpid()>0?0:87); /* authenticated USER trap while A is suspended */
}

static void exec200_irq_retain(void)
{
    struct irq_restart_args args;
    long memory,peer,result;
#if defined(__mc68000__)
    long fpu=0;
    (void)Ssystem(S_GETCOOKIE,0x5f465055L,&fpu);
#endif
    memory=Malloc(EXEC200_IRQ_STACK);
    if(memory<=0)exec200_fail("IRQ", "allocate peer stack", memory);
    peer=exec200_irq_thread(exec200_irq_peer,(void *)memory);
    if(peer<=0)exec200_fail("IRQ", "publish peer", peer);
    irq_restart_prepare_args(&args,(uintptr_t)clone_irq_registers,(uintptr_t)clone_irq_registers_end);
    result=exec200_probe(&args,IRT_BEGIN);
    if(result)exec200_fail("IRQ", "arm the owned frame", result);
#if defined(__mc68000__)
    result=clone_irq_registers(fpu);
#else
    result=clone_irq_registers();
#endif
    if(result)exec200_fail("IRQ", "restore register pattern", result);
    if(exec200_probe(0,IRT_QUERY)!=IRT_ACKED)exec200_fail("IRQ", "missing B2 witness", 0);
    exec200_irq_join(peer,(void *)memory);
}
#endif
