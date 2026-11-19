/* Production timer policy: no private BEGIN selector, syscall or voluntary
 * yield in either worker's progress loop. Serial execution cannot complete
 * the alternating handshake. Both PROCESS and THREAD ownership are exercised. */
#if defined(TEST_WITH_NATIVE_PREEMPT)
#include <stddef.h>
#if defined(TEST_WITH_NATIVE_PREEMPT_PROBES) && !defined(TEST_WITH_PRIVATE_CLONE_TEST)
#include "../../include/mapping_lifetime_test.h"
static long mlt_snapshot(MAPPING_LIFETIME_SNAPSHOT *snapshot)
{ return pclone_trap(snapshot,MLT_SNAPSHOT); }
static void mlt_print(const char *name,const MAPPING_LIFETIME_SNAPSHOT *snapshot)
{
    unsigned i;char text[64];conws(name);
    for(i=0;i<MLT_FIELDS;i++) {sprintf(text," %lu",(unsigned long)snapshot->value[i]);conws(text);}
    conws("\r\n");
}
#endif
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
#if defined(TEST_WITH_NATIVE_PREEMPT_PROBES)
static void preempt_image_patterns(void)
{
    long child[2],report[2][3],own_root=pclone_trap(0,0xc8920100UL),fd;
    unsigned i,attempt;
    int okay=1;
    const char *files[2]={"PREEMPR0.DAT","PREEMPR1.DAT"};
    (void)pclone_trap(0,0xc713c101UL);
    (void)Fdelete("PREEMPG.ON");
    for(i=0;i<2;i++)(void)Fdelete(files[i]);
    for(i=0;i<2;i++) {
        child[i]=Pexec(100,"PREEMPT.TOS",i?"\1" "1":"\1" "0",0);
        ptest_assert_msg(child[i]>0,"loader publishes independent timer/FPU PROCESS");
        if(child[i]<=0)okay=0;
    }
    memset(report,0,sizeof(report));
    for(i=0;i<2;i++) {
        fd=-1;
        for(attempt=0;child[i]>0 && attempt<1024;attempt++) {
            long bytes=0;
            fd=Fopen(files[i],0);
            if(fd>=0) {bytes=Fread((short)fd,sizeof(report[i]),report[i]);(void)Fclose((short)fd);}
            if(bytes==sizeof(report[i]))break;
            (void)Syield();
        }
        if(attempt==1024 || child[i]<=0)okay=0;
    }
    {
        int distinct=own_root>0 && report[0][0]>0 && report[1][0]>0 &&
            report[0][0]!=report[1][0] && report[0][0]!=own_root && report[1][0]!=own_root &&
            report[0][1]==child[0] && report[1][1]==child[1];
        ptest_assert_msg(distinct,"register peers have distinct live PROCESS and MMU roots");
        if(!distinct)okay=0;
    }
#if defined(__mc68000__)
    ptest_assert_msg(report[0][2]==0x80000L && report[1][2]==0x80000L,
                     "both 040 peers execute actual internal-FPU patterns");
    if(report[0][2]!=0x80000L || report[1][2]!=0x80000L)okay=0;
#endif
    fd=Fcreate("PREEMPG.ON",0);
    if(fd>=0)(void)Fclose((short)fd);else okay=0;
    for(i=0;i<2;i++)if(child[i]>0) {
        long status=Pwaitpid(child[i],0,0);
        int success=(status>>16)==child[i] && !(status&65535);
        if(!success) {
            char message[96];
            sprintf(message,"timer image role=%u PID=%ld status=%lx root=%lx\r\n",
                    i,child[i],(unsigned long)status,(unsigned long)report[i][0]);
            conws(message);
        }
        ptest_assert_msg(success,"independently loaded timer peer preserves GPR/FPU patterns");
        if(!success)okay=0;
    }
    {
        long patterns=pclone_trap(0,0xc713c102UL);
        ptest_assert_msg(patterns==3,"both hardware pattern captures returned after another THREAD ran USER");
        if(patterns==3 && okay)
            conws("native timer original/alternate GPR/FPU delayed restoration: PASS\r\n");
        else okay=0;
    }
    for(i=0;i<2;i++)ptest_assert_msg(Fdelete(files[i])==0,"retire root identity report");
    ptest_assert_msg(Fdelete("PREEMPG.ON")==0,"retire independent image pattern gate");
    conws(okay?"native timer independent-image/root GPR/FPU patterns: PASS\r\n":
               "native timer independent-image/root GPR/FPU patterns: FAIL\r\n");
}
#endif
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
#if defined(TEST_WITH_NATIVE_PREEMPT_PROBES)
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
#if defined(TEST_WITH_PTERMGRP) && defined(TEST_WITH_NATIVE_PREEMPT_PROBES)
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
#if defined(__arm__)
/* Separate pages exceed the architectural exclusive-reservation granule.
 * The peer publishes its handshake after leaving a fresh LDREX on the same
 * counter. A must not consume that peer-owned CPU reservation with STREX. */
static volatile unsigned long preempt_exclusive_word[1024] __attribute__((aligned(4096)));
static volatile unsigned long preempt_exclusive_stage[1024] __attribute__((aligned(4096)));
static volatile unsigned long preempt_exclusive_result;
static void preempt_exclusive_a(void *unused)
{
    unsigned long observed,status,candidate=1,budget=1000000000UL;
    (void)unused;
    __asm__ volatile("ldrex %0,[%1]" : "=&r"(observed) : "r"(preempt_exclusive_word) : "memory");
    preempt_exclusive_stage[0]=1;
    while(preempt_exclusive_stage[0]!=2 && --budget) __asm__ volatile("" ::: "memory");
    __asm__ volatile("strex %0,%1,[%2]" : "=&r"(status) : "r"(candidate),"r"(preempt_exclusive_word) : "memory");
    preempt_exclusive_result=budget && !observed && status && preempt_exclusive_word[0]==7;
    preempt_exclusive_stage[0]=3;
}
static void preempt_exclusive_b(void *unused)
{
    unsigned long observed,budget=1000000000UL;
    (void)unused;
    while(preempt_exclusive_stage[0]!=1 && --budget) __asm__ volatile("" ::: "memory");
    preempt_exclusive_word[0]=7;
    __asm__ volatile("ldrex %0,[%1]" : "=&r"(observed) : "r"(preempt_exclusive_word) : "memory");
    preempt_exclusive_stage[0]=2;
    while(preempt_exclusive_stage[0]!=3 && --budget) __asm__ volatile("" ::: "memory");
    (void)observed;
}
static void preempt_exclusive_ownership(void)
{
    long a,b;int32_t pid=0,tid[2]={0,0};
    preempt_exclusive_word[0]=preempt_exclusive_stage[0]=preempt_exclusive_result=0;
    a=launch_entry(preempt_exclusive_a,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[0],&pid,&tid[0]);
    b=launch_entry(preempt_exclusive_b,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[1],&pid,&tid[1]);
    ptest_assert_msg(a>0 && b>0,"publish native ARM exclusive-reservation peers");
    if(a>0)ptest_assert_msg(join_tid(tid[0],0)==0,"exclusive owner A returns");
    if(b>0)ptest_assert_msg(join_tid(tid[1],0)==0,"exclusive owner B returns");
    ptest_assert_msg(preempt_exclusive_result && preempt_exclusive_word[0]==7,
                     "STREX cannot consume another THREAD's LDREX reservation");
    if(a>0 && b>0 && preempt_exclusive_result && preempt_exclusive_word[0]==7)
        conws("native timer ARM exclusive monitor ownership: PASS\r\n");
}
#endif
static void test_preempt(void)
{
    int okay;
#if defined(__arm__)
    preempt_exclusive_ownership();
#endif
#if defined(TEST_WITH_NATIVE_PREEMPT_PROBES)
    preempt_image_patterns();
#endif
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
#if defined(TEST_WITH_NATIVE_PREEMPT_PROBES)
    preempt_blocked_wakeup();
#if defined(TEST_WITH_PTERMGRP)
    preempt_cancel_retirement();
#endif
#endif
}
#endif
