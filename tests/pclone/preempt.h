/* Production timer policy: no private BEGIN selector, syscall or voluntary
 * yield in either worker's progress loop. Serial execution cannot complete
 * the alternating handshake. Both PROCESS and THREAD ownership are exercised. */
#if defined(TEST_WITH_NATIVE_PREEMPT)
#include <stddef.h>
static volatile unsigned long preempt_turn,preempt_steps[2];
struct preempt_worker {
    unsigned role;volatile unsigned error;
    volatile unsigned long *turn,*steps;
    volatile int32_t *pid,*tid;
};
typedef char preempt_loop_layout[offsetof(struct preempt_worker,turn)==8 &&
                                offsetof(struct preempt_worker,steps)==12?1:-1];
static struct preempt_worker preempt_workers[2];
static void preempt_entry(void *argument)
{
    struct preempt_worker *worker=argument;
    unsigned round,role=worker->role;
    volatile unsigned long local=0x713c0000UL|role;
    if(*worker->pid<=0 || *worker->tid<=0) {worker->error=4;return;}
    for (round=0;round<16;round++) {
        unsigned long budget=1000000000UL;
        while (*worker->turn!=role) {
            if (!--budget) { worker->error=1;return; }
        }
        if (local!=(0x713c0000UL|role) || worker->steps[role]!=round) worker->error=2;
        worker->steps[role]=round+1;
        *worker->turn=1-role;
    }
}
static int preempt_run(void (*entry)(void *))
{
    unsigned policy,i;
    int32_t pids[2]={0,0},tids[2]={0,0};
    int okay=1;
    for (policy=0;policy<2;policy++) {
        preempt_turn=preempt_steps[0]=preempt_steps[1]=0;
        for (i=0;i<2;i++) {
            struct ptos_clone_args args={0};
            long result;
            preempt_workers[i].role=i;preempt_workers[i].error=0;
            preempt_workers[i].turn=&preempt_turn;preempt_workers[i].steps=preempt_steps;
            preempt_workers[i].pid=&pids[i];preempt_workers[i].tid=&tids[i];
            args.flags=CLONE_VM|(policy?CLONE_SAME_PROCESS:0);
            args.entry=entry;args.argument=&preempt_workers[i];
            args.stack_base=runnable_stacks[i];args.stack_size=RUNNABLE_STACK_SIZE;
            args.child_pid=&pids[i];args.child_tid=&tids[i];
            result=pclone_trap(&args,sizeof(args));
            ptest_assert_msg(result>0,"CPU-bound timer worker admitted");
            if (result<=0) return 0;
        }
        for (i=0;i<2;i++) {
            long result=policy?trap_1_wll(GEMDOS_PTHJOIN,tids[i],0):Pwaitpid(pids[i],0,0);
            int success=policy?result==0:(result>>16)==pids[i] && !(result&65535);
            ptest_assert_msg(success,"CPU-bound worker exits through its existing owner");
            ptest_assert_msg(preempt_steps[i]==16 && !preempt_workers[i].error,
                             "timer preemption progresses both CPU-bound users without Syield");
            if (!success || preempt_steps[i]!=16 || preempt_workers[i].error) {
                char message[128];
                sprintf(message,"timer progress policy=%u worker=%u steps=%lu/%lu error=%u status=%lx\r\n",
                        policy,i,preempt_steps[0],preempt_steps[1],preempt_workers[i].error,(unsigned long)result);
                conws(message);okay=0;
            }
        }
    }
    return okay;
}
#if defined(TEST_WITH_PFORK) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void preempt_private_patterns(void)
{
    unsigned i;
    long child[2],fd;
    int okay=1;
#if defined(__mc68000__)
    extern long clone_preempt_registers(long),clone_preempt_registers_alt(long);
#else
    extern long clone_preempt_registers(void),clone_preempt_registers_alt(void);
#endif
    (void)Fdelete("PREEMPG.ON");
    for(i=0;i<2;i++) {
        child[i]=Pfork();
        if (!child[i]) {
            unsigned cycle,attempt=0;
            int error=0;
            do {
                fd=Fopen("PREEMPG.ON",0);
                if(fd<0) (void)Syield(); /* admission gate, outside the register window */
            } while(fd<0 && ++attempt<1024);
            if(fd<0) { (void)Pterm(84);return; }
            (void)Fclose((short)fd);
            for(cycle=0;cycle<4;cycle++) {
#if defined(__mc68000__)
                error|=i?clone_preempt_registers_alt(has_fpu):clone_preempt_registers(has_fpu);
#else
                error|=i?clone_preempt_registers_alt():clone_preempt_registers();
#endif
            }
            (void)Pterm(error?83:0);
            return;
        }
        ptest_assert_msg(child[i]>0,"public Pfork prepares independent timer/FPU context");
        if(child[i]<=0)okay=0;
    }
    fd=Fcreate("PREEMPG.ON",0);
    ptest_assert_msg(fd>=0,"release both privately owned register-pattern peers");
    if(fd>=0)(void)Fclose((short)fd);else okay=0;
    for(i=0;i<2;i++) if(child[i]>0) {
        long status=Pwaitpid(child[i],0,0);
        int success=(status>>16)==child[i] && !(status&65535);
        ptest_assert_msg(success,"timer-preempted private root preserves original/alternate GPR and FPU state");
        if(!success)okay=0;
    }
    ptest_assert_msg(Fdelete("PREEMPG.ON")==0,"retire register-peer file gate");
    conws(okay?"native timer public-Pfork private-root GPR/FPU patterns: PASS\r\n":
               "native timer public-Pfork private-root GPR/FPU patterns: FAIL\r\n");
}
#endif
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static volatile unsigned long preempt_wait_started,preempt_wait_woke,preempt_wait_progress;
static void preempt_wait_spinner(void *unused)
{
    unsigned long budget=1000000000UL;
    (void)unused;preempt_wait_started=1;
    while(!preempt_wait_woke && --budget) preempt_wait_progress++;
}
static void preempt_wait_sleeper(void *unused)
{
    long result;
    (void)unused;
    result=pclone_trap((const void *)(uintptr_t)8,0xc713c100UL);
    preempt_wait_woke=result?2:1;
}
static void preempt_blocked_wakeup(void)
{
    int32_t pid=0,tid[2]={0,0};
    long a,b;
    preempt_wait_started=preempt_wait_woke=preempt_wait_progress=0;
    a=launch_entry(preempt_wait_spinner,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[0],&pid,&tid[0]);
    b=launch_entry(preempt_wait_sleeper,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[1],&pid,&tid[1]);
    ptest_assert_msg(a>0 && b>0,"publish busy USER and independently blocked sleeper");
    if(a>0)ptest_assert_msg(join_tid(tid[0],0)==0,"busy worker returns after timer wakes sleeper");
    if(b>0)ptest_assert_msg(join_tid(tid[1],0)==0,"blocked sleeper rejoins through its ordinary owner");
    ptest_assert_msg(preempt_wait_started && preempt_wait_woke==1 && preempt_wait_progress,
                     "timer expiry services blocked readiness while USER never yields");
    if(a>0 && b>0 && preempt_wait_started && preempt_wait_woke==1 && preempt_wait_progress)
        conws("native timer busy USER and blocked sleeper wake: PASS\r\n");
}
#endif
#if defined(TEST_WITH_PTERMGRP) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
static volatile unsigned long preempt_cancel_started,preempt_cancel_seen,preempt_cancel_after;
static void preempt_cancel_spinner(void *unused)
{
    unsigned long budget=1000000000UL;
    (void)unused;preempt_cancel_started=1;
    while(--budget) __asm__ volatile("" ::: "memory");
    preempt_cancel_after=1;
}
static void preempt_cancel_killer(void *unused)
{
    unsigned long budget=1000000000UL;
    (void)unused;
    while(!preempt_cancel_started && --budget) __asm__ volatile("" ::: "memory");
    preempt_cancel_seen=preempt_cancel_started;
    (void)trap_1_ww(GEMDOS_PTERMGRP,61);
    preempt_cancel_after=1;
}
static void preempt_cancel_leader(void *unused)
{
    int32_t pid=0,tid[2]={0,0};
    long a,b;
    (void)unused;
    a=launch_entry(preempt_cancel_spinner,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[1],&pid,&tid[0]);
    b=launch_entry(preempt_cancel_killer,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],&pid,&tid[1]);
    if(a<=0 || b<=0) { (void)Pterm(98);return; }
    (void)join_tid(tid[0],0);
    preempt_cancel_after=1;
}
static void preempt_cancel_retirement(void)
{
    int32_t pid=0,tid=0;
    long created,status;
    MAPPING_LIFETIME_SNAPSHOT before,after;
    unsigned i;
    int same=1;
    preempt_cancel_started=preempt_cancel_seen=preempt_cancel_after=0;
    for(i=0;i<3;i++) {
        unsigned long page;
        for(page=0;page<RUNNABLE_STACK_SIZE;page+=4096)runnable_stacks[i][page]=0;
    }
    (void)mlt_snapshot(&before);(void)mlt_snapshot(&after);(void)mlt_snapshot(&before);
    created=launch_entry(preempt_cancel_leader,CLONE_VM,runnable_stacks[0],&pid,&tid);
    ptest_assert_msg(created>0,"publish automatic suspended-cancellation PROCESS");
    status=created>0?Pwaitpid(pid,0,0):-1;
    ptest_assert_msg((status>>16)==pid && (status&65535)==61 &&
                     preempt_cancel_seen && !preempt_cancel_after,
                     "group exit cancels suspended IRQ before another USER instruction");
    ptest_assert_msg(mlt_snapshot(&after)==0,"snapshot after automatic cancellation retirement");
    for(i=0;i<MLT_FIELDS;i++)if(before.value[i]!=after.value[i])same=0;
    if(!same) {mlt_print("timer cancellation before",&before);mlt_print("timer cancellation after",&after);}
    ptest_assert_msg(same,"automatic suspended cancellation conserves all owners exactly");
    if(created>0 && (status>>16)==pid && (status&65535)==61 &&
       preempt_cancel_seen && !preempt_cancel_after && same)
        conws("native timer suspended-group cancellation and exact retirement: PASS\r\n");
}
#endif
static void test_preempt(void)
{
    int okay;
#if defined(TEST_WITH_PFORK) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
    preempt_private_patterns();
#endif
    okay=preempt_run(preempt_entry);
    conws(okay?"native timer CPU-bound PROCESS/THREAD progress without Syield: PASS\r\n":
               "native timer CPU-bound PROCESS/THREAD progress without Syield: FAIL\r\n");
#if defined(TEST_WITH_MMAP)
    {
        extern void preempt_file_loop(void *);
        extern char preempt_file_loop_end[];
        static unsigned char page[4096];
        unsigned long bytes=(uintptr_t)preempt_file_loop_end-(uintptr_t)preempt_file_loop;
        long fd,mapping;
        ptest_assert_msg(bytes && bytes<sizeof(page),"position-independent mapped-code fixture fits one page");
        if (!bytes || bytes>=sizeof(page)) return;
        memcpy(page,(const void *)(uintptr_t)preempt_file_loop,bytes);
        fd=Fcreate("PREEMPC.BIN",0);
        ptest_assert_msg(fd>=0,"create mapped executable timer fixture");
        if (fd<0) return;
        ptest_assert_msg(Fwrite((short)fd,sizeof(page),page)==sizeof(page),"write mapped executable timer fixture");
        mapping=clone_test_sys6(GEMDOS_MMAP,0,4096,PROT_READ|PROT_EXEC,MAP_PRIVATE,fd,0);
        ptest_assert_msg(mapping>0,"map owned file-cache executable leaf");
        if (mapping>0) {
            okay=preempt_run((void (*)(void *))(uintptr_t)mapping);
            conws(okay?"native timer file-backed executable progress without Syield: PASS\r\n":
                       "native timer file-backed executable progress without Syield: FAIL\r\n");
            ptest_assert_msg(clone_test_sys6(GEMDOS_MUNMAP,mapping,4096,0,0,0,0)==0,
                             "file execution holds retire before unmapping");
        }
        ptest_assert_msg(Fclose((short)fd)==0 && Fdelete("PREEMPC.BIN")==0,
                         "file execution backing and directory ownership close independently");
    }
#endif
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    preempt_blocked_wakeup();
#if defined(TEST_WITH_PTERMGRP)
    preempt_cancel_retirement();
#endif
#endif
}
#endif
