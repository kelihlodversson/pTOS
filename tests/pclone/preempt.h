/* Production timer policy: no private BEGIN selector, syscall or voluntary
 * yield in either worker's progress loop. Serial execution cannot complete
 * the alternating handshake. Both PROCESS and THREAD ownership are exercised. */
#if defined(TEST_WITH_NATIVE_PREEMPT)
static volatile unsigned long preempt_turn,preempt_steps[2];
struct preempt_worker { unsigned role;volatile unsigned error; };
static struct preempt_worker preempt_workers[2];
static void preempt_entry(void *argument)
{
    struct preempt_worker *worker=argument;
    unsigned round,role=worker->role;
    volatile unsigned long local=0x713c0000UL|role;
    for (round=0;round<16;round++) {
        unsigned long budget=1000000000UL;
        while (preempt_turn!=role) {
            if (!--budget) { worker->error=1;return; }
        }
        if (local!=(0x713c0000UL|role) || preempt_steps[role]!=round) worker->error=2;
        preempt_steps[role]=round+1;
        preempt_turn=1-role;
    }
}
static void test_preempt(void)
{
    unsigned policy,i;
    int32_t pids[2],tids[2];
    int okay=1;
    for (policy=0;policy<2;policy++) {
        preempt_turn=preempt_steps[0]=preempt_steps[1]=0;
        for (i=0;i<2;i++) {
            struct ptos_clone_args args={0};
            long result;
            preempt_workers[i].role=i;preempt_workers[i].error=0;
            args.flags=CLONE_VM|(policy?CLONE_SAME_PROCESS:0);
            args.entry=preempt_entry;args.argument=&preempt_workers[i];
            args.stack_base=runnable_stacks[i];args.stack_size=RUNNABLE_STACK_SIZE;
            args.child_pid=&pids[i];args.child_tid=&tids[i];
            result=pclone_trap(&args,sizeof(args));
            ptest_assert_msg(result>0,"CPU-bound timer worker admitted");
            if (result<=0) { okay=0;return; }
        }
        for (i=0;i<2;i++) {
            long result=policy?trap_1_wll(GEMDOS_PTHJOIN,tids[i],0):Pwaitpid(pids[i],0,0);
            int success=policy?result==0:(result>>16)==pids[i] && !(result&65535);
            ptest_assert_msg(success,"CPU-bound worker exits through its existing owner");
            ptest_assert_msg(preempt_steps[i]==16 && !preempt_workers[i].error,
                             "timer preemption progresses both CPU-bound users without Syield");
            if (!success || preempt_steps[i]!=16 || preempt_workers[i].error) okay=0;
        }
    }
    conws(okay?"native timer CPU-bound PROCESS/THREAD progress without Syield: PASS\r\n":
               "native timer CPU-bound PROCESS/THREAD progress without Syield: FAIL\r\n");
}
#endif
