/* Actual managed PROCESSes, GEMDOS mappings and architecture launchers. The
 * scoped prefix is a regression-kernel adapter to the trusted factory only. */
#if defined(TEST_WITH_PRIVATE_CLONE_TEST) && defined(TEST_WITH_MMAP)
#include "../../include/mapping_lifetime_test.h"
static struct {
    volatile int mode,error,ready,go,seen,done;
    volatile long parent,grandpid;
    volatile unsigned char *mapping,*anonymous,*heap,*guard;
    MAPPING_LIFETIME_CLAIM claim[3],spent;
    MAPPING_LIFETIME_SNAPSHOT during;
} mlt;
static MAPPING_LIFETIME_SNAPSHOT mlt_initial,mlt_before,mlt_after;
static MAPPING_LIFETIME_SNAPSHOT mlt_created,mlt_retained;
static MAPPING_LIFETIME_SNAPSHOT mlt_start,mlt_completed,mlt_work;
static unsigned char mlt_file_data[4096];
static unsigned char *mlt_stack;

static long mlt_map(long address,long bytes,long protection,long flags,long fd)
{ return clone_test_sys6(GEMDOS_MMAP,address,bytes,protection,flags,fd,0); }
static long mlt_unmap(long address,long bytes)
{ return clone_test_sys6(GEMDOS_MUNMAP,address,bytes,0,0,0,0); }
static long mlt_protect(long address,long bytes,long protection)
{ return clone_test_sys6(GEMDOS_MPROT,address,bytes,protection,0,0,0); }
static long mlt_snapshot(MAPPING_LIFETIME_SNAPSHOT *snapshot)
{ return pclone_trap(snapshot,MLT_SNAPSHOT); }
static long mlt_claim(unsigned long operation,MAPPING_LIFETIME_CLAIM *claim)
{ return pclone_trap(claim,operation); }
static void mlt_print(const char *name,const MAPPING_LIFETIME_SNAPSHOT *snapshot)
{
    unsigned i;
    char text[64];
    conws(name);
    for(i=0;i<MLT_FIELDS;i++) {
        sprintf(text," %lu",(unsigned long)snapshot->value[i]);conws(text);
    }
    conws("\r\n");
}
static void mlt_equal(const MAPPING_LIFETIME_SNAPSHOT *a,const MAPPING_LIFETIME_SNAPSHOT *b)
{
    unsigned i;
    for(i=0;i<MLT_FIELDS;i++) if(a->value[i]!=b->value[i]) {
        char text[96];
        sprintf(text,"mapping resource field %u: %lu -> %lu\r\n",i,
                (unsigned long)a->value[i],(unsigned long)b->value[i]);conws(text);
        ptest_assert_msg(0,"exact steady-root resource conservation");
    }
}
static long mlt_launch(void (*entry)(void *),void *stack,unsigned flags,int scoped,int32_t *pid,int32_t *tid)
{
    struct ptos_clone_args args={0};
    args.flags=flags;args.entry=entry;args.stack_base=stack;args.stack_size=32768;
    args.child_pid=pid;args.child_tid=tid;
    return pclone_trap(&args,(scoped?MLT_SCOPED_CLONE:0)+sizeof(args));
}
static void mlt_survivor(void *argument)
{
    (void)argument;
    mlt.seen=1;
    while(!mlt.go) {
        if(mlt.ready && mlt.anonymous && mlt.anonymous[17]!=0x63) mlt.error=30;
        (void)Syield();
    }
    if(mlt.anonymous && mlt.anonymous[17]!=0x63) mlt.error=31;
    mlt.done=1;Pterm(38);
}
static void mlt_child(void *argument)
{
    long fd,mapping,anonymous,heap,guard,nested_stack;
    int32_t pid=-1,tid=-1;
    unsigned i;
    (void)argument;
    if(Pgetpid()==mlt.parent) { mlt.error=1;return; }
    if(mlt.mode==1) {
        anonymous=mlt_map(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1);
        if(anonymous<=0) { mlt.error=2;return; }
        mlt.anonymous=(unsigned char *)anonymous;mlt.anonymous[17]=0x63;mlt.ready=1;
        return;
    }
    if(mlt.mode>=6) {
        nested_stack=Malloc(32768);
        if(nested_stack<=0) { mlt.error=3;return; }
        memset((void *)nested_stack,0,32768);
        if(mlt_launch(mlt_survivor,(void *)nested_stack,CLONE_VM|(mlt.mode==8?0:CLONE_SAME_PROCESS),0,&pid,&tid)<=0)
            { mlt.error=4;return; }
        mlt.grandpid=pid;
        while(!mlt.seen) (void)Syield();
    }
    fd=Fopen("C:\\MLTMAP.TMP",0);
    mapping=fd<0?fd:mlt_map(0,12288,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd);
    if(fd>=0 && Fclose(fd)) mlt.error=5;
    anonymous=mlt_map(0,12288,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1);
    heap=Malloc(12288);
    guard=mlt_map(0,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1);
    if(mapping<=0 || anonymous<=0 || heap<=0 || guard<=0) { mlt.error=6;return; }
    mlt.mapping=(unsigned char *)mapping;mlt.anonymous=(unsigned char *)anonymous;
    mlt.heap=(unsigned char *)heap;mlt.guard=(unsigned char *)guard;
    mlt.anonymous[17]=0x63;mlt.heap[17]=0x64;
    /* The middle file page remains untouched for a retained peer's late fault. */
    if(mlt.mapping[17]!=0x31) mlt.error=7;
    mlt.mapping[17]=0x71; /* real clean-file COW */
    if(mlt_protect(anonymous+4096,4096,PROT_READ) ||
       mlt_protect(anonymous+4096,4096,PROT_READ|PROT_WRITE)) mlt.error=8;
    if(mlt.mode==2) {
        long addresses[3]={mapping+4096,heap,guard};
        long lengths[3]={4096,12288,4096};
        for(i=0;i<3;i++) {
            MAPPING_LIFETIME_CLAIM bad;
            memset(&mlt.claim[i],0,sizeof(mlt.claim[i]));
            mlt.claim[i].address=addresses[i];mlt.claim[i].bytes=lengths[i];
            if(mlt_claim(MLT_INFO,&mlt.claim[i])) { mlt.error=9;return; }
            bad=mlt.claim[i];bad.birth++;
            if(mlt_claim(MLT_ATTACH_PARENT,&bad)!=-36L || bad.ticket) mlt.error=10;
            if(mlt_claim(MLT_ATTACH_PARENT,&mlt.claim[i])) { mlt.error=11;return; }
            bad=mlt.claim[i];
            if(mlt_claim(MLT_DETACH,&bad)!=-36L || bad.ticket!=mlt.claim[i].ticket) mlt.error=12;
        }
    } else if(mlt_unmap(mapping+4096,4096)) mlt.error=13;
    if(mlt_snapshot(&mlt.during)) mlt.error=14;
    mlt.ready=1;
    if(mlt.mode==3) *(volatile unsigned long *)0=1; /* supported fault, no fini */
    if(mlt.mode==4) { Pterm0();return; }
    if(mlt.mode==5 || mlt.mode==6) { Pterm(37);return; }
#if defined(TEST_WITH_PTERMGRP)
    if(mlt.mode==7) { (void)term_group(61);mlt.error=15; }
#endif
}

static void mlt_run(int mode,int conserve)
{
    int32_t pid=-1,tid=-1;
    long result,status;
    int spins;
    ptest_assert_msg(mlt_snapshot(&mlt_start)==0,"snapshot before PROCESS construction");
    memset(&mlt,0,sizeof(mlt));mlt.mode=mode;mlt.parent=Pgetpid();
    result=mlt_launch(mlt_child,mlt_stack,CLONE_VM,mode!=1,&pid,&tid);
    ptest_assert_msg(result>0 && result==tid && pid!=mlt.parent,"real scoped/common PROCESS publication");
    if(result<=0) return;
    if(mode==6) {
        for(spins=0;spins<128 && !mlt.ready;spins++) (void)Syield();
        (void)Syield();
        ptest_assert_msg(mlt.ready && !mlt.error && Pwaitpid(pid,1,0)==0,
                         "leader exit preserves PROCESS mappings and last THREAD");
        mlt.go=1;
    }
    if(mode!=6 && mode!=8) {
        for(spins=0;spins<128;spins++) {
            ptest_assert_msg(mlt_snapshot(&mlt_completed)==0,"snapshot before canonical reap");
            if(mlt_completed.value[MLT_PROCESSES]==mlt_start.value[MLT_PROCESSES] &&
               mlt_completed.value[MLT_THREADS]==mlt_start.value[MLT_THREADS]) break;
            (void)Syield();
        }
        ptest_assert_msg(spins<128 && mlt.ready,"PROCESS retires independently of parent Pwaitpid");
        ptest_assert_msg(mlt_completed.value[MLT_HEAP]==mlt_start.value[MLT_HEAP]+(mode==2?1:0) &&
                         mlt_completed.value[MLT_MMAP]==mlt_start.value[MLT_MMAP]+(mode==2?2:mode==1?1:0) &&
                         mlt_completed.value[MLT_BYTES]==mlt_start.value[MLT_BYTES]+(mode==2?8192:mode==1?4096:0),
                         "completion retains only legitimate cohort/peer mappings before reap");
    }
    status=Pwaitpid(pid,0,0);
    ptest_assert_msg((status>>16)==pid && !mlt.error && mlt.ready,"mapped PROCESS completes once without ownership error");
    if(mode==3) ptest_assert_msg((status&65535)!=0,"real fault terminates before userspace finalization");
    if(mode!=3 && mode!=6) ptest_assert_msg((status&65535)==(mode==5?37:mode==7?61:0),"normal/Pterm/Pterm0/group status preserved");
    ptest_assert_msg(Pwaitpid(pid,1,0)==-33L,"one canonical PROCESS reap");
    if(mode==1) {
        ptest_assert_msg(mlt.anonymous[17]==0x63,"late common-VM raw pointer survives allocator exit");
        ptest_assert_msg(mlt_unmap((long)mlt.anonymous,4096)==0,"surviving cohort member may explicitly unmap");
    }
    if(mode==2) {
        MAPPING_LIFETIME_CLAIM spent=mlt.claim[0];
        long replacement,view,fd;
        mlt_created=mlt.during;
        ptest_assert_msg(mlt_snapshot(&mlt_retained)==0,"snapshot after creator completion before peer detach");
        ptest_assert_msg(mlt.mapping[4096+17]==0x31 && mlt.heap[17]==0x64,
                         "peer faults untouched file page and retains creator heap after exit");
        fd=Fopen("C:\\MLTMAP.TMP",0);
        view=fd<0?fd:clone_test_sys6(GEMDOS_MMAP,0,4096,PROT_READ,MAP_PRIVATE,fd,4096);
        if(fd>=0) ptest_assert_msg(Fclose(fd)==0,"independent lazy file view closes descriptor");
        ptest_assert_msg(view>0 && ((volatile unsigned char *)view)[17]==0x31,
                         "independent peer file view remains readable");
        ptest_assert_msg(mlt_snapshot(&mlt_work)==0 &&
                         mlt_work.value[MLT_FILE_PAGES]==mlt_retained.value[MLT_FILE_PAGES]+1 &&
                         mlt_work.value[MLT_FILE_REFS]==mlt_retained.value[MLT_FILE_REFS]+2,
                         "two clean views hold exactly one shared physical file page");
        mlt.mapping[4096+17]=0x72;
        ptest_assert_msg(view>0 && ((volatile unsigned char *)view)[17]==0x31 &&
                         mlt_unmap(view,4096)==0,"retained peer COW write preserves independent file view");
        ptest_assert_msg(mlt_protect((long)mlt.mapping+4096,4096,PROT_READ)==0 &&
                         mlt_protect((long)mlt.mapping+4096,4096,PROT_READ|PROT_WRITE)==0,
                         "lifetime attachment preserves actual COW and permission changes");
        ptest_assert_msg(mlt_protect((long)mlt.guard,4096,PROT_READ|PROT_WRITE)==0 && !mlt.guard[17],
                         "PROT_NONE lifetime claim does not fabricate access");
        for(spins=0;spins<3;spins++)
            ptest_assert_msg(mlt_claim(MLT_DETACH,&mlt.claim[spins])==0 && !mlt.claim[spins].ticket,
                             "last exact attachment reclaims its reservation/backing");
        replacement=mlt_map(spent.address,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1);
        ptest_assert_msg(replacement==(long)spent.address && mlt_claim(MLT_DETACH,&spent)==-36L,
                         "consumed ticket cannot retire a replacement at the same address");
        if(replacement>0) {
            *(volatile unsigned char *)replacement=0x56;
            ptest_assert_msg(*(volatile unsigned char *)replacement==0x56 && mlt_unmap(replacement,4096)==0,
                             "replacement retains independent live birth");
        }
    }
    if(mode==8) {
        ptest_assert_msg(!mlt.done && mlt.anonymous[17]==0x63,
                         "generic clone retains scoped creator's future mappings after parent completion");
        mlt.go=1;
        for(spins=0;spins<128 && !mlt.done;spins++) (void)Syield();
        (void)Syield();(void)Syield();
        ptest_assert_msg(mlt.done && !mlt.error && Pwaitpid(mlt.grandpid,1,0)==-33L,
                         "orphan final consumer retires mappings without grandparent reaping");
    }
    ptest_assert_msg(mlt_snapshot(&mlt_after)==0,"read-only post-completion resource snapshot");
    if(conserve) mlt_equal(&mlt_before,&mlt_after);
}

static struct {
    volatile int ready,go,entered,returned,owner_done,error,cancel;
} mlt_contention;
static MAPPING_LIFETIME_SNAPSHOT mlt_wait_before,mlt_wait_after;
#if defined(TEST_WITH_PTERMGRP)
static void mlt_cancel_waiter(void *argument)
{
    (void)argument;
    while(!mlt_contention.entered) (void)Syield();
    (void)term_group(61);mlt_contention.error=41;
}
#endif
static void mlt_waiter(void *argument)
{
    long allocation;
    (void)argument;
#if defined(TEST_WITH_PTERMGRP)
    if(mlt_contention.cancel) {
        int32_t pid=-1,tid=-1;
        long stack=Malloc(32768);
        if(stack<=0) { mlt_contention.error=42;return; }
        memset((void *)stack,0,32768);
        if(mlt_launch(mlt_cancel_waiter,(void *)stack,CLONE_VM|CLONE_SAME_PROCESS,0,&pid,&tid)<=0 || pid!=Pgetpid())
            { mlt_contention.error=43;return; }
    }
#endif
    mlt_contention.ready=1;
    while(!mlt_contention.go) (void)Syield();
    mlt_contention.entered=1;
    allocation=Malloc(12288);
    mlt_contention.returned=1;
    if(mlt_contention.cancel || allocation<=0) mlt_contention.error=44;
}
static void mlt_guard_owner(void *argument)
{
    long allocation;
    (void)argument;
    mlt_contention.go=1;
    /* Real Malloc reaches a scheduling checkpoint with its metadata guard
     * held. The regression adapter never exposes a held guard to user mode. */
    allocation=pclone_trap(0,MLT_CONTENDED_ALLOC);
    if(allocation<=0) mlt_contention.error=45;
    mlt_contention.owner_done=1;
}
static void mlt_test_contention(void)
{
    void *waiter_stack=(void *)Malloc(32768);
    unsigned round;
    int spins;
    int32_t owner_pid=-1,owner_tid=-1,waiter_pid=-1,waiter_tid=-1;
    long status,waits;
    ptest_assert_msg((long)waiter_stack>0,"allocate competing native continuation stack");
    if((long)waiter_stack<=0) return;
    memset(waiter_stack,0,32768);
    memset(&mlt_wait_before,0,sizeof(mlt_wait_before));memset(&mlt_wait_after,0,sizeof(mlt_wait_after));
    for(round=0;round<4;round++) {
        memset(&mlt_contention,0,sizeof(mlt_contention));
#if defined(TEST_WITH_PTERMGRP)
        mlt_contention.cancel=round&1;
#endif
        waits=pclone_trap(0,MLT_WAIT_COUNT);
        ptest_assert_msg(mlt_snapshot(&mlt_wait_before)==0,"contention pre-construction snapshot");
        ptest_assert_msg(mlt_launch(mlt_waiter,waiter_stack,CLONE_VM,1,&waiter_pid,&waiter_tid)>0,
                         "publish independent guarded waiter PROCESS");
        for(spins=0;spins<128 && !mlt_contention.ready && !mlt_contention.error;spins++) (void)Syield();
        ptest_assert_msg(mlt_contention.ready && !mlt_contention.error,"waiter and optional cancellation sibling ready");
        ptest_assert_msg(mlt_launch(mlt_guard_owner,mlt_stack,CLONE_VM,1,&owner_pid,&owner_tid)>0,
                         "publish guard owner PROCESS");
        status=Pwaitpid(owner_pid,0,0);
        ptest_assert_msg((status>>16)==owner_pid && !(status&65535) && mlt_contention.owner_done,
                         "guard owner resumes and settles transfer after contention");
        status=Pwaitpid(waiter_pid,0,0);
        ptest_assert_msg((status>>16)==waiter_pid && (status&65535)==(mlt_contention.cancel?61:0) &&
                         !mlt_contention.error && mlt_contention.returned==!mlt_contention.cancel &&
                         pclone_trap(0,MLT_WAIT_COUNT)>waits,
                         "blocked peer resumes, or cancellation wins without user return/publication");
        ptest_assert_msg(mlt_snapshot(&mlt_wait_after)==0,"contention post-stop snapshot");
        if(round>=2) mlt_equal(&mlt_wait_before,&mlt_wait_after);
    }
    ptest_assert_msg(Mfree(waiter_stack)==0,"release idle contention fixture stack");
    if(!ptest_failures()) {
        conws("native shared-root guarded peer contention: PASS\r\n");
#if defined(TEST_WITH_PTERMGRP)
        conws("native shared-root guarded waiter cancellation: PASS\r\n");
#endif
    }
}

static void test_mapping_lifetime(void)
{
    long file,result,status;
    unsigned i,stage;
    int32_t pid,tid;
    ptest_begin("shared-root mapping lifetime");
    mlt_stack=(unsigned char *)Malloc(32768);
    ptest_assert_msg((long)mlt_stack>0,"allocate native mapping-lifetime launcher stack");
    if((long)mlt_stack<=0) return;
    memset(mlt_stack,0,32768);memset(&mlt_initial,0,sizeof(mlt_initial));
    memset(&mlt_before,0,sizeof(mlt_before));memset(&mlt_after,0,sizeof(mlt_after));
    memset(&mlt_created,0,sizeof(mlt_created));memset(&mlt_retained,0,sizeof(mlt_retained));
    memset(&mlt_start,0,sizeof(mlt_start));memset(&mlt_completed,0,sizeof(mlt_completed));memset(&mlt_work,0,sizeof(mlt_work));
    memset(mlt_file_data,0x31,sizeof(mlt_file_data));
    file=Fcreate("C:\\MLTMAP.TMP",0);
    ptest_assert_msg(file>=6,"create native file-backed lifetime fixture");
    if(file<6) { (void)Mfree(mlt_stack);return; }
    for(i=0;i<3;i++) ptest_assert_msg(Fwrite(file,4096,mlt_file_data)==4096,"populate real file pages");
    ptest_assert_msg(Fclose(file)==0,"close fixture before lazy mappings");
    ptest_assert_msg(mlt_snapshot(&mlt_initial)==0,"initial read-only resource snapshot");
    /* Record initial and primed root metadata separately. Do not reset any
     * counters; subsequent exact comparisons include every live resource. */
    for(i=0;i<=8;i++) {
#if !defined(TEST_WITH_PTERMGRP)
        if(i==7) continue;
#endif
        mlt_run(i,0);
    }
    mlt_test_contention();
    ptest_assert_msg(mlt_snapshot(&mlt_before)==0,"primed resource baseline");
    mlt_print("mapping initial",&mlt_initial);mlt_print("mapping baseline",&mlt_before);
    pid=0x12345678;tid=0x23456789;
    memset(&mlt,0,sizeof(mlt));mlt.parent=Pgetpid();
    ptest_assert_msg(pclone_trap(0,0xc8920600UL)==0,"request cancellation after sealed preparation");
    result=mlt_launch(mlt_child,mlt_stack,CLONE_VM,1,&pid,&tid);
    ptest_assert_msg(result==-326L && pid==0x12345678 && tid==0x23456789 && !mlt.ready,
                     "sealed scoped publication cancellation leaves no child or outputs");
    ptest_assert_msg(mlt_snapshot(&mlt_after)==0,"cancelled publication resource snapshot");mlt_equal(&mlt_before,&mlt_after);
    for(stage=1;stage<128;stage++) {
        pid=0x12345678;tid=0x23456789;
        ptest_assert_msg(pclone_trap(0,0xc9670200UL+stage)==0,"inject ownership preparation failure");
        memset(&mlt,0,sizeof(mlt));mlt.parent=Pgetpid();
        result=mlt_launch(mlt_child,mlt_stack,CLONE_VM,1,&pid,&tid);
        (void)pclone_trap(0,0xc9670200UL);
        if(result>0) { status=Pwaitpid(pid,0,0);ptest_assert_msg((status>>16)==pid,"successful endpoint after full ownership fail sweep");break; }
        ptest_assert_msg(result==-39L && pid==0x12345678 && tid==0x23456789 && !mlt.ready,
                         "failed scope/dependency construction publishes no identity or user output");
        ptest_assert_msg(mlt_snapshot(&mlt_after)==0,"rollback resource snapshot");mlt_equal(&mlt_before,&mlt_after);
    }
    ptest_assert_msg(stage<128,"ownership fail sweep reaches successful complete construction");
    for(i=0;i<128;i++) {
        int mode=(int)(i%9);
#if !defined(TEST_WITH_PTERMGRP)
        if(mode==7) mode=0;
#endif
        mlt_run(mode,1);
    }
    mlt_print("mapping created",&mlt_created);mlt_print("mapping retained",&mlt_retained);
    mlt_print("mapping final",&mlt_after);
    ptest_assert_msg(Mfree(mlt_stack)==0 && Fdelete("C:\\MLTMAP.TMP")==0,"native lifetime fixture final cleanup");
    ptest_pass();
    if(!ptest_failures()) {
        conws("native shared-root common/scoped/late mapping lifetime: PASS\r\n");
        conws("native shared-root peer/last-THREAD/orphan safety: PASS\r\n");
        conws("native shared-root exact mapping resource conservation: PASS\r\n");
    }
}
#endif
