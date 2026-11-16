/* Actual user-trap image-policy controls in a small companion. The complete
 * self-reloading regression image still fits the unchanged classic window. */
#include "test.h"
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#include "../../include/shared_pexec_test.h"
#include "../../include/image_policy_test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long pclone_trap(const void *args, unsigned long size)
{
#if defined(__mc68000__)
    register long result __asm__("d0");
    __asm__ volatile (
        "move.l %2,-(%%sp)\n\t"
        "move.l %1,-(%%sp)\n\t"
        "move.w #0x190,-(%%sp)\n\t"
        "trap #1\n\t"
        "lea 10(%%sp),%%sp"
        : "=d"(result) : "r"(args), "r"(size)
        : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
    return result;
#elif defined(__arm__)
    register long r0 __asm__("r0") = GEMDOS_PCLONE;
    register const void *r1 __asm__("r1") = args;
    register unsigned long r2 __asm__("r2") = size;
    __asm__ volatile ("svc 1" : "+r"(r0) : "r"(r1), "r"(r2)
                      : "r3", "r12", "lr", "memory", "cc");
    return r0;
#else
    return trap_1_wll(GEMDOS_PCLONE,(long)args,(long)size);
#endif
}
static volatile long image_policy_digest,image_policy_pid,image_policy_tid,image_policy_error;
static void image_policy_entry(void *argument)
{
    (void)argument;
    image_policy_pid=Pgetpid();image_policy_tid=pclone_trap(0,0xc8920103UL);
    if (pclone_trap(0,IMAGE_POLICY_QUERY_DIGEST)!=image_policy_digest || pclone_trap(0,IMAGE_POLICY_QUERY_INSTALLED)!=1)
        image_policy_error=1;
    (void)Pterm(71);
}
static int image_policy_snapshot(SHARED_PEXEC_SNAPSHOT *snapshot)
{
    memset(snapshot,0,sizeof(*snapshot));
    return pclone_trap(snapshot,SHARED_PEXEC_SNAPSHOT_QUERY)==0;
}
static int image_policy_same(const SHARED_PEXEC_SNAPSHOT *before,const SHARED_PEXEC_SNAPSHOT *after)
{
    int i,ok=1;
    for (i=0;i<SP_COUNT;i++) if(before->count[i]!=after->count[i]) {
        printf("image policy conservation[%d]: %lu -> %lu\r\n",i,
            (unsigned long)before->count[i],(unsigned long)after->count[i]);ok=0;
    }
    return ok;
}
static void test_image_policy(void)
{
    struct ptos_clone_args args={0};
    SHARED_PEXEC_SNAPSHOT before,during,after;
    long stack=Malloc(32768),tid,result,parent=Pgetpid();
    int cycle,stage,ok=1;
    ptest_begin("immutable executable policy through existing clone activation");
    image_policy_digest=pclone_trap(0,IMAGE_POLICY_QUERY_DIGEST);
    ptest_assert_msg(stack>0 && image_policy_digest>=0 && pclone_trap(0,IMAGE_POLICY_QUERY_INSTALLED)==0,
                     "authenticate original executable policy");
    if(stack<=0 || image_policy_digest<0) { ptest_pass();return; }
    memset((void *)stack,0,32768);
    args.flags=CLONE_VM;args.entry=image_policy_entry;args.stack_base=(void *)stack;args.stack_size=32768;
    /* First exercise all owner paths so retained allocator/table caches are
     * part of the baseline, never reset or hidden by destroying the root. */
    for(cycle=0;cycle<10;cycle++) {
        image_policy_pid=image_policy_tid=image_policy_error=0;
        if(!image_policy_snapshot(&before)) ok=0;
        if(cycle>=2) for(stage=1;stage<=3;stage++) {
            (void)pclone_trap(0,stage==3?0xc8920600UL:IMAGE_POLICY_FAIL_AFTER+stage);
            result=pclone_trap(&args,IMAGE_POLICY_CLONE|sizeof(args));
            if(result!=(stage==3?-326L:-39L)) ok=0;
            if(!image_policy_snapshot(&after) || !image_policy_same(&before,&after) ||
               image_policy_pid || image_policy_tid || image_policy_error) ok=0;
        }
        tid=pclone_trap(&args,IMAGE_POLICY_CLONE|sizeof(args));
        if(tid<=0) {ok=0;break;}
        if(!image_policy_snapshot(&during) || during.count[SP_PROCESSES]!=before.count[SP_PROCESSES]+1 ||
            during.count[SP_THREADS]!=before.count[SP_THREADS]+1 || during.count[SP_SPACES]!=before.count[SP_SPACES] ||
            during.count[SP_SPACE_REFS]<=before.count[SP_SPACE_REFS] ||
            during.count[SP_PROCESS_REFS]<=before.count[SP_PROCESS_REFS]) ok=0;
        result=Pwaitpid(-1,0,0);
        if ((result>>16)!=image_policy_pid || (result&65535)!=71 || image_policy_pid==parent ||
            image_policy_tid!=tid || image_policy_error) ok=0;
        if(!image_policy_snapshot(&after) || (cycle>=2 && !image_policy_same(&before,&after))) ok=0;
    }
    args.flags|=CLONE_SAME_PROCESS;
    if(pclone_trap(&args,IMAGE_POLICY_CLONE|sizeof(args))!=-25L) ok=0;
    ptest_assert_msg(ok,"value policy activates once; two preparation faults, cancelled publication and all counters conserve");
    ptest_assert_msg(Mfree((void *)stack)==0,"image policy releases its initial stack hold");
    if(ok) conws("native immutable image policy capture/activation/rollback and exact resource conservation: PASS\r\n");
    ptest_pass();
}

/* Runs both in ordinary loaded images and in the PE_LOAD -> mode 106 moved
 * image below. Child status crosses the private VM boundary; globals do not. */
static volatile long private_image_value=0x13579;
static void private_image_entry(void *argument)
{
    long expected=(long)argument;
    int bad=private_image_value!=0x13579 || Pgetpid()==expected;
    private_image_value=0x24680;
    (void)Pterm(bad?79:72);
}
static void test_private_image(void)
{
    struct ptos_clone_args args={0};
    long stack=Malloc(32768),tid,status;
    int cycle,ok=1;
    ptest_begin("private image ownership and COW execution");
    if(stack<=0) {ptest_assert_msg(0,"allocate private image stack");ptest_pass();return;}
    args.flags=CLONE_VM;args.entry=private_image_entry;args.argument=(void *)Pgetpid();
    args.stack_base=(void *)stack;args.stack_size=32768;
    for(cycle=0;cycle<4;cycle++) {
        memset((void *)stack,0,32768);private_image_value=0x13579;
        tid=pclone_trap(&args,0xc8920000UL|sizeof(args));
        if(tid<=0) {ok=0;break;}
        status=Pwaitpid(-1,0,0);
        if((status&65535)!=72 || private_image_value!=0x13579) ok=0;
    }
    ptest_assert_msg(ok,"independent image child executes with isolated COW writes");
    ptest_assert_msg(Mfree((void *)stack)==0,"release private image stack");
    if(ok) conws("native private image execution/COW: PASS\r\n");
    ptest_pass();
}

int main(void)
{
    int failures;
    ptest_log_append();
    test_image_policy();
    test_private_image();
    failures=ptest_failures();
    ptest_log_flush();
    Pterm(failures);
    return failures;
}
