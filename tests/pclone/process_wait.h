/* Public MiNT waits through the native GEMDOS trap on each MMU backend. */
static volatile struct {
    long release, fault, entered[2], result[2];
    int32_t child;
} process_wait_probe;

static void process_wait_child(void *argument)
{
    (void)argument;
    while (!process_wait_probe.release) (void)Syield();
    if (process_wait_probe.fault) *(volatile long *)0x80001000UL=1;
    Pterm(0x1234);
}

#if defined(TEST_WITH_PTHJOIN)
static void process_wait_contend(unsigned index)
{
    process_wait_probe.entered[index]=1;
    process_wait_probe.result[index]=Pwaitpid(process_wait_probe.child,0,0);
}

static void process_wait_first(void *argument)
{
    (void)argument;
    process_wait_contend(0);
}

static void process_wait_second(void *argument)
{
    (void)argument;
    process_wait_contend(1);
}

static void test_process_wait_contention(void)
{
    int32_t child=0,tids[2]={0,0};
    long launched[2],expected,first,second;
    unsigned spin;

    process_wait_probe.release=0;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[0],&child,NULL)<=0) {
        ptest_fail("create PROCESS for competing waits");
        return;
    }
    process_wait_probe.child=child;
    launched[0]=launch_entry(process_wait_first,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[1],NULL,&tids[0]);
    launched[1]=launch_entry(process_wait_second,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],NULL,&tids[1]);
    for (spin=0; spin<128 && (!process_wait_probe.entered[0] || !process_wait_probe.entered[1]); spin++)
        (void)Syield();
    ptest_assert_msg(launched[0]>0 && launched[1]>0 && process_wait_probe.entered[0] &&
                     process_wait_probe.entered[1] && !process_wait_probe.result[0] &&
                     !process_wait_probe.result[1],"both THREADs wait on the same live child PROCESS");
    process_wait_probe.release=1;
    if (launched[0]>0) ptest_assert_msg(!join_tid(tids[0],NULL),"join the first PROCESS waiter");
    if (launched[1]>0) ptest_assert_msg(!join_tid(tids[1],NULL),"join the second PROCESS waiter");
    expected=((long)child<<16)|0x34;
    first=process_wait_probe.result[0];
    second=process_wait_probe.result[1];
    ptest_assert_msg((first==expected && second==-33L) || (second==expected && first==-33L),
                     "one public waiter consumes the status and the other observes no child");
    ptest_assert_msg(Pwaitpid(child,1,0)==-33L,"competing waits leave no unreaped status");
}
#endif

static void test_process_wait(void)
{
    long usage[4]={-101,-102,-103,-104};
    const long bad_usage=0x80001000UL;
    long status;
    int32_t child=0;
    unsigned spin;

    ptest_assert_msg(Pwait()==-33L && Pwait3(1,bad_usage)==-33L,
                     "no children leave even an invalid output untouched");
    process_wait_probe.release=1;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[0],&child,NULL)<=0) {
        ptest_fail("create PROCESS for Pwait");
        return;
    }
    ptest_assert_msg(Pwait()==(((long)child<<16)|0x34) && Pwaitpid(child,1,0)==-33L,
                     "Pwait returns the child PID and low-byte normal status exactly once");

    process_wait_probe.release=0;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[0],&child,NULL)<=0) {
        ptest_fail("create PROCESS for Pwait3 and Pwaitpid");
        return;
    }
    ptest_assert_msg(Pwait3(1L,usage+1)==0 && Pwaitpid(child,1,bad_usage)==0 &&
                     usage[1]==-102 && usage[2]==-103,"WNOHANG leaves usage alone while the child is live");
    ptest_assert_msg(Pwaitpid(child,4,0)==-25L && Pwaitpid(0,0,0)==-32L &&
                     Pwaitpid(-2,0,0)==-32L,"validate wait options and reject unsupported group selectors");
    process_wait_probe.release=1;
    status=0;
    for (spin=0; spin<128 && !status; spin++) {
        (void)Syield();
        status=Pwait3(1,bad_usage);
    }
    ptest_assert_msg(status==-40L,"late nonblocking Pwait3 rejects invalid usage and keeps the zombie");
#if defined(TEST_WITH_MMAP)
    {
        long mapping=clone_test_sys6(GEMDOS_MMAP,0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        ptest_assert_msg(mapping>0,"map two pages for wait usage copyout");
        if (mapping>0) {
            long *crossing=(long *)(mapping+4096-sizeof(long));

            crossing[0]=-105;crossing[1]=-106;
            ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT,mapping+4096,4096,PROT_READ,0,0,0)==0,
                             "protect the second word of wait usage output");
            ptest_assert_msg(Pwaitpid(child,0,crossing)==-40L && crossing[0]==-105 && crossing[1]==-106,
                             "cross-page usage failure changes neither word and preserves status");
            ptest_assert_msg(clone_test_sys6(GEMDOS_MUNMAP,mapping,8192,0,0,0,0)==0,
                             "failed wait copy releases the usage mapping lease");
        }
    }
#endif
    ptest_assert_msg(Pwait3(0,usage+1)==(((long)child<<16)|0x34) && usage[0]==-101 &&
                     usage[1]>=0 && usage[2]==0 && usage[3]==-104 && Pwaitpid(child,1,0)==-33L,
                     "valid retry copies exactly two usage words before reaping");

    process_wait_probe.fault=1;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[0],&child,NULL)<=0) {
        ptest_fail("create PROCESS for fault status");
        return;
    }
    usage[1]=-102;usage[2]=-103;
    ptest_assert_msg(Pwaitpid(child,0,usage+1)==(((long)child<<16)|(11<<8)) && usage[0]==-101 &&
                     usage[1]>=0 && usage[2]==0 && usage[3]==-104 && Pwaitpid(child,1,0)==-33L,
                     "Pwaitpid reports fault signal in the high status byte with usage");
    process_wait_probe.fault=0;
#if defined(TEST_WITH_PTHJOIN)
    test_process_wait_contention();
#endif
}
