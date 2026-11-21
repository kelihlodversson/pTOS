/* Public MiNT waits through the native GEMDOS trap on each MMU backend. */
#include "../../include/process_identity.h"
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
#include "../../include/mapping_lifetime_test.h"
#endif

#define WAIT_BAD_USAGE 0x80001000UL

static volatile struct {
    long release, fault, timed, result[2];
    int32_t child;
} process_wait_probe;

static long process_wait_identity(long pid,PROCESS_IDENTITY *identity)
{
    memset(identity,0,sizeof(*identity));
    identity->selector=PROCESS_IDENTITY_PID;
    identity->pid=pid;
    return Ssystem(-8,identity,sizeof(*identity)); /* S_PROCIDENT */
}

static void process_wait_child(void *argument)
{
    (void)argument;
    while (!process_wait_probe.release) (void)Syield();
    if (process_wait_probe.fault) *(volatile long *)0x80001000UL=1;
    if (process_wait_probe.timed) {
        unsigned long start=(unsigned long)Ssystem(S_GETLVAL,0x4baL,0L);
        unsigned long spin;

        /* The exported accounting is tick-based. Wait for nonzero usage so
         * credit applied on failure or twice on retry would be detected. */
        for (spin=0; spin<65536UL && (unsigned long)Ssystem(S_GETLVAL,0x4baL,0L)-start<2; spin++)
            (void)Syield();
    }
    Pterm(0x1234);
}

static void process_wait_zombie_parent(void *argument)
{
    int32_t child=0;

    (void)argument;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[2],&child,NULL)<=0) {
        Pterm(1);
        return;
    }
    process_wait_probe.child=child;
    Pterm(Pwaitpid(child,0,WAIT_BAD_USAGE)==-40L ? 73 : 2);
}

static void test_process_wait_zombie_orphan(void)
{
    PROCESS_IDENTITY identity;
    int32_t parent=0;
    long found=0;
    unsigned spin;

    process_wait_probe.child=0;
    if (launch_entry(process_wait_zombie_parent,CLONE_VM,runnable_stacks[0],&parent,NULL)<=0) {
        ptest_fail("create parent of an abandoned zombie");
        return;
    }
    ptest_assert_msg(Pwaitpid(parent,0,0)==(((long)parent<<16)|73) && process_wait_probe.child>0,
                     "parent exits after proving that its child has an unreaped status");
    if (!process_wait_probe.child) return;
    for (spin=0; spin<128 && !found; spin++) {
        found=process_wait_identity(process_wait_probe.child,&identity);
        if (!found) (void)Syield();
    }
    ptest_assert_msg(found==-20L && Pwaitpid(process_wait_probe.child,1,0)==-33L,
                     "orphaned zombie is retired without transferring its status to the grandparent");
}

#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void test_process_wait_copy_oom(long *usage)
{
    static MAPPING_LIFETIME_SNAPSHOT before,after;

    memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
    ptest_assert_msg(pclone_trap(&before,MLT_SNAPSHOT)==0,"snapshot before wait usage allocation failure");
    ptest_assert_msg(pclone_trap(usage,MLT_FAIL_COPY)==0,"arm the waiter's exact usage allocation failure");
    ptest_assert_msg(Pwait3(1,usage)==-39L && usage[0]==-102 && usage[1]==-103,
                     "usage lease allocation failure leaves both words and the pending result untouched");
    ptest_assert_msg(pclone_trap(&after,MLT_SNAPSHOT)==0 && !memcmp(&before,&after,sizeof(before)),
                     "failed wait restores all PROCESS, THREAD, ASPACE, mapping and allocator counts");
}
#endif

#if defined(TEST_WITH_PTHJOIN) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void process_wait_contend(unsigned index)
{
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

static void process_wait_foreign(void *argument)
{
    (void)argument;
    Pterm(Pwaitpid(process_wait_probe.child,1,0)==-33L ? 0 : 1);
}

static void test_process_wait_contention(void)
{
    int32_t child=0,peer=0,tids[2]={0,0};
    long launched[2],expected,first,second,blocked=0;
    unsigned spin;

    process_wait_probe.release=0;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[0],&child,NULL)<=0) {
        ptest_fail("create PROCESS for competing waits");
        return;
    }
    process_wait_probe.child=child;
    first=launch_entry(process_wait_foreign,CLONE_VM,runnable_stacks[1],&peer,NULL);
    ptest_assert_msg(first>0 && Pwaitpid(peer,0,0)==((long)peer<<16) && Pwaitpid(child,1,0)==0,
                     "shared ASPACE gives a peer no wait right and both PROCESSes remain separately waitable");
    ptest_assert_msg(pclone_trap((void *)(uintptr_t)child,MLT_PROCESS_WAITERS)==0,
                     "live child without an admitted waiter is not counted as a wait");
    launched[0]=launch_entry(process_wait_first,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[1],NULL,&tids[0]);
    launched[1]=launch_entry(process_wait_second,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],NULL,&tids[1]);
    for (spin=0; spin<128; spin++) {
        blocked=pclone_trap((void *)(uintptr_t)child,MLT_PROCESS_WAITERS);
        if (blocked==2) break;
        (void)Syield();
    }
    ptest_assert_msg(launched[0]>0 && launched[1]>0 && blocked==2 &&
                     !process_wait_probe.result[0] && !process_wait_probe.result[1],
                     "both THREADs are blocked in Pwaitpid on the same live child PROCESS");
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
    long parent_usage[8],credited,status;
    PROCESS_IDENTITY zombie,again;
    int32_t child=0;
    unsigned long spin;

    ptest_assert_msg(Pwait()==-33L && Pwait3(1,WAIT_BAD_USAGE)==-33L,
                     "no children leave even an invalid output untouched");
    process_wait_probe.release=1;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[0],&child,NULL)<=0) {
        ptest_fail("create PROCESS for Pwait");
        return;
    }
    ptest_assert_msg(Pwait()==(((long)child<<16)|0x34) && Pwaitpid(child,1,0)==-33L,
                     "Pwait returns the child PID and low-byte normal status exactly once");

    process_wait_probe.release=0;
    process_wait_probe.timed=1;
    if (launch_entry(process_wait_child,CLONE_VM,runnable_stacks[0],&child,NULL)<=0) {
        ptest_fail("create PROCESS for Pwait3 and Pwaitpid");
        return;
    }
    ptest_assert_msg(Pwait3(1,usage+1)==0 && Pwaitpid(child,1,WAIT_BAD_USAGE)==0 &&
                     usage[1]==-102 && usage[2]==-103,"WNOHANG leaves usage alone while the child is live");
    ptest_assert_msg(Pwaitpid(child,4,0)==-25L && Pwaitpid(0,0,0)==-32L &&
                     Pwaitpid(-2,0,0)==-32L,"validate wait options and reject unsupported group selectors");
    process_wait_probe.release=1;
    status=0;
    for (spin=0; spin<65536UL && !status; spin++) {
        (void)Syield();
        status=Pwait3(1,WAIT_BAD_USAGE);
    }
    ptest_assert_msg(status==-40L,"late nonblocking Pwait3 rejects invalid usage and keeps the zombie");
    ptest_assert_msg(process_wait_identity(child,&zombie)==0 && zombie.pid==child,
                     "unreaped PROCESS identity remains discoverable after exit");
    ptest_assert_msg(Prusage(parent_usage)==0,"read cumulative child usage before retry");
    credited=parent_usage[3];
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    test_process_wait_copy_oom(usage+1);
#endif
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
    ptest_assert_msg(process_wait_identity(child,&again)==0 && !memcmp(&zombie,&again,sizeof(zombie)) &&
                     Prusage(parent_usage)==0 && parent_usage[3]==credited,
                     "failed deliveries retain the same identity and charge no child usage");
    ptest_assert_msg(Pwait3(0,usage+1)==(((long)child<<16)|0x34) && usage[0]==-101 &&
                     usage[1]>0 && usage[2]==0 && usage[3]==-104 && Pwaitpid(child,1,0)==-33L,
                     "valid retry copies exactly two usage words before reaping");
    ptest_assert_msg(Prusage(parent_usage)==0 && parent_usage[3]==credited+usage[1] &&
                     process_wait_identity(child,&again)==-20L,
                     "one successful reap charges usage once and releases the zombie identity");
    process_wait_probe.timed=0;

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
    test_process_wait_zombie_orphan();
#if defined(TEST_WITH_PTHJOIN) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
    test_process_wait_contention();
#endif
}
