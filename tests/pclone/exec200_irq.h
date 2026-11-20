/* Run an authentic retained return on a heap-backed USER stack. */
#ifndef TEST_EXEC200_IRQ_H
#define TEST_EXEC200_IRQ_H
#include "irq_restart_probe.h"

#define EXEC200_IRQ_STACK 32768
#define EXEC200_IRQ_GATE "GOIRQ.ON"
#if defined(__mc68000__)
extern long clone_irq_registers(long fpu);
#else
extern long clone_irq_registers(void);
#endif

static long exec200_irq_spawn(void (*entry)(void *), void *stack, int32_t *pid)
{
    struct ptos_clone_args args={0};
    args.flags=CLONE_VM;
    args.entry=entry;
    args.stack_base=stack;
    args.stack_size=EXEC200_IRQ_STACK;
    args.child_pid=pid;
    return exec200_probe(&args,sizeof(args));
}

static void exec200_irq_peer(void *unused)
{
    (void)unused;
    Pterm(Pgetpid()>0?0:87); /* authenticated USER trap while A is suspended */
}

static void exec200_irq_reap(int32_t pid,void *stack)
{
    long result=Pwaitpid(pid,0,0);
    if((result>>16)!=pid || (result&65535) || Mfree(stack))
        exec200_fail("IRQ", "reap peer and release stack", result);
}

static void exec200_irq_retain(void)
{
    struct irq_restart_args args;
    long memory,peer,result;
    int32_t pid=0;
#if defined(__mc68000__)
    long fpu=0;
    (void)Ssystem(S_GETCOOKIE,0x5f465055L,&fpu);
#endif
    memory=Malloc(EXEC200_IRQ_STACK);
    if(memory<=0)exec200_fail("IRQ", "allocate peer stack", memory);
    /* Replacing a private clone requires a never-threaded PROCESS. Use a
     * separate peer PROCESS on the same root, then reap it before Pexec. */
    peer=exec200_irq_spawn(exec200_irq_peer,(void *)memory,&pid);
    if(peer<=0 || pid<=0)exec200_fail("IRQ", "publish peer", peer);
    irq_restart_prepare_args(&args,(uintptr_t)clone_irq_window_begin,(uintptr_t)clone_irq_window_end);
    result=exec200_probe(&args,IRT_BEGIN);
    if(result)exec200_fail("IRQ", "arm the owned frame", result);
#if defined(__mc68000__)
    result=clone_irq_registers(fpu);
#else
    result=clone_irq_registers();
#endif
    if(result)exec200_fail("IRQ", "restore register pattern", result);
    if(exec200_probe(0,IRT_QUERY)!=IRT_ACKED)exec200_fail("IRQ", "missing B2 witness", 0);
    exec200_irq_reap(pid,(void *)memory);
}
#endif
