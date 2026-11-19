/* Production timer policy: no private BEGIN selector, syscall or voluntary
 * yield in either worker's progress loop. Serial execution cannot complete
 * the alternating handshake. Both PROCESS and THREAD ownership are exercised. */
#if defined(TEST_WITH_NATIVE_PREEMPT)
#include <stddef.h>
static volatile unsigned long preempt_turn,preempt_steps[2];
struct preempt_worker {
    unsigned role;volatile unsigned error;
    volatile unsigned long *turn,*steps;
};
typedef char preempt_loop_layout[offsetof(struct preempt_worker,turn)==8 &&
                                offsetof(struct preempt_worker,steps)==12?1:-1];
static struct preempt_worker preempt_workers[2];
static void preempt_entry(void *argument)
{
    struct preempt_worker *worker=argument;
    unsigned round,role=worker->role;
    volatile unsigned long local=0x713c0000UL|role;
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
    int32_t pids[2],tids[2];
    int okay=1;
    for (policy=0;policy<2;policy++) {
        preempt_turn=preempt_steps[0]=preempt_steps[1]=0;
        for (i=0;i<2;i++) {
            struct ptos_clone_args args={0};
            long result;
            preempt_workers[i].role=i;preempt_workers[i].error=0;
            preempt_workers[i].turn=&preempt_turn;preempt_workers[i].steps=preempt_steps;
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
static void test_preempt(void)
{
    int okay=preempt_run(preempt_entry);
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
}
#endif
