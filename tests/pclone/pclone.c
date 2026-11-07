/* User-mode Pclone checks through the real GEMDOS trap, not kernel calls. */
#include "test.h"
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#include <mint/osbind.h>
#include <stdio.h>
#include <mint/mintbind.h>

#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_USER_ASPACE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
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
static volatile int entry_ran;
static void child_entry(void *argument) { (void)argument; entry_ran = 1; }
#endif

#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
/* These are actual user continuations; Pclone cannot pass by just constructing
 * kernel identities. A blocked PROCESS wait must dispatch the child. */
long clone_registers(unsigned long seed);
#if defined(__mc68000__)
long clone_fpu_registers(unsigned long seed);
long clone_fpu_initial(void);
unsigned long clone_fpu_observed, clone_fpu_expected_iar;
#endif
static long has_fpu;
static unsigned long stack_trim;
static unsigned char *runnable_stacks[3];
#define RUNNABLE_STACK_SIZE 32768UL
static volatile struct clone_probe {
    long pid, parent, steps, error;
    unsigned long local;
    int mode;
    volatile long gate, done, child_pid, child_tid;
} probe;
static void runnable_entry(void *argument)
{
    volatile struct clone_probe *p = argument;
    volatile unsigned long local = 0x13579bdfUL;
    int i;
#if defined(__mc68000__)
    if (has_fpu && clone_fpu_initial()) p->error |= 1;
#endif
    p->pid = Pgetpid();
    p->local = (unsigned long)&local;
    for (i = 0; i < 12; i++) {
        if (local != 0x13579bdfUL) p->error = 1;
        p->steps++;
#if defined(__arm__) || defined(__mc68000__)
        if (clone_registers((unsigned long)&local)) p->error |= 2;
#else
        (void)Syield(); /* x32 GP/FPU probes remain in the native MEMTEST */
#endif
#if defined(__mc68000__)
        if (has_fpu) {
            long error = clone_fpu_registers((unsigned long)&local);
            if (error) p->error |= 4 | (error << 4);
        }
#endif
    }
    if (p->mode == 1) Pterm(23);
    if (p->mode == 2) *(volatile unsigned long *)0x80001000UL = 1;
}
static long launch_entry(void (*entry)(void *), unsigned flags, unsigned char *stack, int32_t *pid, int32_t *tid)
{
    struct ptos_clone_args args = {0};
    args.flags = flags;
    args.entry = entry;
    args.argument = (void *)&probe;
    args.stack_base = stack;
    args.stack_size = RUNNABLE_STACK_SIZE - stack_trim;
    args.child_pid = pid; args.child_tid = tid;
    return pclone_trap(&args, sizeof(args));
}
static long launch_probe(unsigned flags, int stack, int32_t *pid, int32_t *tid)
{
    return launch_entry(runnable_entry, flags, runnable_stacks[stack], pid, tid);
}
static void survivor_entry(void *argument)
{
    volatile struct clone_probe *p = argument;
    int i;
    for (i = 0; i < 8; i++) (void)Syield();
    p->done = 1;
    Pterm(31);
}
static void leader_entry(void *argument)
{
    volatile struct clone_probe *p = argument;
    int32_t pid = -1, tid = -1;
    long result = launch_entry(survivor_entry,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],&pid,&tid);
    if (result <= 0 || pid != Pgetpid()) p->error = 1;
    p->child_tid = tid;
    /* Both identities are live here, before leader-first exit. */
    if (trap_1_wlw(0x31,0L,0) != -36L) p->error = 1;
    Pterm(17);
}
static void orphan_entry(void *argument)
{
    volatile struct clone_probe *p = argument;
    while (!p->gate) (void)Syield();
    p->done = 1;
}
static void retiring_parent(void *argument)
{
    volatile struct clone_probe *p = argument;
    int32_t pid = -1, tid = -1;
    if (launch_entry(orphan_entry,CLONE_VM,runnable_stacks[2],&pid,&tid) <= 0) p->error = 1;
    p->child_pid = pid;
}
static void exhaustion_entry(void *argument)
{
    volatile struct clone_probe *p = argument;
    while (!p->gate) (void)Syield();
    p->done++;
}
static void test_lifecycle(void)
{
    int32_t pid = -1, tid = -1;
    long status, result;
    int i, made;
    unsigned char *stacks[12];
#if defined(__mc68000__)
    {
        long output = Malloc(8192);
        ptest_assert_msg(output > 0,"cross-page output allocation");
        if (output > 0) {
            int32_t *crossing = (int32_t *)(output + 4094);
            *(volatile char *)output = 0;
            *(volatile char *)(output+4096) = 0;
            *crossing = -99;
            probe.steps = probe.error = 0; probe.mode = 0;
            result = launch_entry(runnable_entry,CLONE_VM,runnable_stacks[0],crossing,&tid);
            ptest_assert_msg(result > 0 && *crossing > 0,"native two-byte-aligned cross-page PID publication");
            if (result > 0) {
                status = Pwaitpid(*crossing,0,0);
                ptest_assert_msg((status >> 16) == *crossing && !(status & 65535),"cross-page published PID waits correctly");
            }
            ptest_assert_msg(Mfree((void *)output) == 0,"cross-page output lease released");
        }
    }
#endif
    probe.done = probe.error = 0;
    result = launch_entry(leader_entry,CLONE_VM,runnable_stacks[0],&pid,&tid);
    ptest_assert_msg(result > 0,"leader-exit PROCESS created");
    if (result <= 0) return;
    status = Pwaitpid(pid,0,0);
    ptest_assert_msg((status >> 16) == pid && (status & 65535) == 31 && probe.done && !probe.error,
                     "leader Pterm must preserve survivor; last thread completes PID once");
    ptest_assert_msg(Pwaitpid(pid,1,0) == -33L,"PROCESS zombie can only be consumed once");
    probe.done = probe.error = probe.gate = 0;
    result = launch_entry(retiring_parent,CLONE_VM,runnable_stacks[0],&pid,&tid);
    ptest_assert_msg(result > 0,"orphan controller created");
    if (result <= 0) return;
    status = Pwaitpid(pid,0,0);
    ptest_assert_msg((status >> 16) == pid && !(status & 65535) && !probe.done && !probe.error,
                     "controller may retire while shared-VM child remains blocked");
    probe.gate = 1;
    for (i = 0; i < 64 && !probe.done; i++) (void)Syield();
    (void)Syield(); (void)Syield();
    ptest_assert_msg(probe.done && Pwaitpid(probe.child_pid,1,0) == -33L,
                     "orphan must finish without misdirecting completion to grandparent");
    probe.done = probe.gate = 0;
    for (made = 0; made < 12; made++) {
        long memory = Malloc(RUNNABLE_STACK_SIZE);
        ptest_assert_msg(memory > 0,"exhaustion stack allocation");
        if (memory <= 0) break;
        stacks[made] = (unsigned char *)memory;
        pid = tid = -99;
        result = launch_entry(exhaustion_entry,CLONE_VM|CLONE_SAME_PROCESS,stacks[made],&pid,&tid);
        if (result <= 0) {
            ptest_assert_msg(result == -326L && pid == -99 && tid == -99,
                             "slot exhaustion must preserve output sentinels");
            ptest_assert_msg(Mfree(stacks[made]) == 0,"failed publication must release stack hold");
            break;
        }
        ptest_assert_msg(Mfree(stacks[made]) < 0,"runnable user stack cannot be freed");
    }
    ptest_assert_msg(made > 0 && made < 12 && !probe.done,"bounded scheduler exhausts before dispatch");
    probe.gate = 1;
    for (i = 0; i < 64 && probe.done < made; i++) (void)Syield();
    (void)Syield(); (void)Syield();
    ptest_assert_msg(probe.done == made,"all published siblings finish after exhaustion");
    for (i = 0; i < made; i++)
        ptest_assert_msg(Mfree(stacks[i]) == 0,"root reap releases each stack once");
}
#if defined(TEST_WITH_PTHJOIN)
static struct {
    int32_t target, a, b, leader, survivor;
    volatile long gate, done, entered, result_a, result_b, status_a, error;
    void *output;
} joins;
static long join_tid(long tid, int32_t *status)
{
    return trap_1_wll(GEMDOS_PTHJOIN,tid,(long)status);
}
static void join_target(void *argument)
{
    (void)argument;
    while (!joins.gate) (void)Syield();
    Pterm(57);
}
static void join_a(void *argument)
{
    int32_t status = -1;
    (void)argument;
    if (join_tid(joins.a,0) != -22L) joins.error = 1;
    joins.entered = 1;
    joins.result_a = join_tid(joins.target,&status);
    joins.status_a = status;
}
static void join_b(void *argument)
{
    (void)argument;
    joins.result_b = join_tid(joins.target,0);
    joins.done = 1;
}
static void join_survivor(void *argument)
{
    int32_t status = -1;
    (void)argument;
    if (join_tid(joins.leader,&status) || status != 37) joins.error = 1;
    Pterm(53);
}
static void join_leader(void *argument)
{
    int32_t pid;
    (void)argument;
    if (launch_entry(join_survivor,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],&pid,&joins.survivor) <= 0)
        joins.error = 1;
    Pterm(37);
}
static void join_cycle_a(void *argument)
{
    (void)argument;
    joins.result_a = join_tid(joins.b,0);
}
static void join_cycle_b(void *argument)
{
    (void)argument;
    joins.result_b = join_tid(joins.a,0);
    joins.done = 1;
}
static void join_revoke_output(void *argument)
{
    (void)argument;
    if (Mfree(joins.output)) joins.error = 1;
    Pterm(41);
}
static void test_joins(void)
{
    int32_t pid, tid, status;
    int mode, cycle, spin;
    long result;
    ptest_assert_msg(join_tid(0,0) == -20L && join_tid(-1,0) == -20L &&
                     join_tid(0x7fffffffL,0) == -20L,"invalid/dead TIDs return ESRCH");
    for (cycle = 0; cycle < 3; cycle++) for (mode = 0; mode < 3; mode++) {
        probe.mode = mode; probe.steps = probe.error = 0;
        result = launch_probe(CLONE_VM|CLONE_SAME_PROCESS,0,&pid,&tid);
        ptest_assert_msg(result > 0,"joinable sibling created");
        if (result <= 0) return;
        status = -99;
        ptest_assert_msg(join_tid(tid,(int32_t *)0x80001000UL) == -40L,"bad join output keeps target joinable");
        if (cycle == 1) {
            for (spin=0; spin<128 && probe.steps<12; spin++) (void)Syield();
            (void)Syield(); (void)Syield(); /* join after execution-stack reap */
        }
        ptest_assert_msg(join_tid(tid,&status) == 0 && status == (mode==1 ? 23 : mode==2 ? 65535L : 0),
                         "join return/Pterm/fault status after safe reap");
        ptest_assert_msg(!probe.error && probe.steps == 12,"joining preserves actual execution context");
        ptest_assert_msg(join_tid(tid,0) == -20L,"one successful TID reaper");
    }
    joins.gate = joins.done = joins.entered = joins.error = 0;
    ptest_assert_msg(launch_entry(join_target,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[0],&pid,&joins.target)>0,"join target created");
    ptest_assert_msg(launch_entry(join_a,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[1],&pid,&joins.a)>0,"first joiner created");
    ptest_assert_msg(launch_entry(join_b,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],&pid,&joins.b)>0,"competing joiner created");
    for (cycle=0; cycle<64 && !joins.done; cycle++) (void)Syield();
    ptest_assert_msg(joins.done && joins.entered && joins.result_b==-36L && !joins.error,"exclusive join claim and self rejection");
    ptest_assert_msg(join_tid(joins.target,0)==-36L,"another joiner cannot steal a live claim");
    joins.gate=1;
    ptest_assert_msg(join_tid(joins.a,0)==0 && join_tid(joins.b,0)==0 && joins.result_a==0 && joins.status_a==57,
                     "blocked joiner wakes with correct status");
    ptest_assert_msg(join_tid(joins.target,0)==-20L,"competing target consumed once");
    joins.done=0;
    ptest_assert_msg(launch_entry(join_cycle_a,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[0],&pid,&joins.a)>0,"cycle A created");
    ptest_assert_msg(launch_entry(join_cycle_b,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[1],&pid,&joins.b)>0,"cycle B created");
    for (cycle=0;cycle<64 && !joins.done;cycle++) (void)Syield();
    ptest_assert_msg(joins.done && joins.result_b==-22L,"cyclic join is rejected");
    ptest_assert_msg(join_tid(joins.a,0)==0 && joins.result_a==0,"cycle rejection lets survivor finish");
    joins.output=(void *)Malloc(4096);
    ptest_assert_msg((long)joins.output>0,"revocable join output allocated");
    if ((long)joins.output>0) {
        *(int32_t *)joins.output=-99;
        joins.error=0;
        ptest_assert_msg(launch_entry(join_revoke_output,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[0],&pid,&tid)>0,"output revoker created");
        ptest_assert_msg(join_tid(tid,joins.output)==-40L && !joins.error,"copyout revalidated after blocked wait");
        ptest_assert_msg(join_tid(tid,&status)==0 && status==41,"failed copyout releases claim without consuming status");
    }
    joins.error=0;
    ptest_assert_msg(launch_entry(join_leader,CLONE_VM,runnable_stacks[0],&pid,&joins.leader)>0,"joinable leader PROCESS created");
    ptest_assert_msg(join_tid(joins.leader,0)==-20L,"TID join does not substitute for a different PROCESS wait");
    result=Pwaitpid(pid,0,0);
    ptest_assert_msg((result>>16)==pid && (result&65535)==53 && !joins.error,"survivor joins stopped leader before final PROCESS completion");
    conws("native Pthjoin execution: tested\r\n");
}
#endif
static void test_runnable(void)
{
    int mode, cycle;
    long result, waited;
    int32_t pid, tid, other_tid;
    volatile unsigned long parent_local = 0x2468ace0UL;
    long usage[8], baseline_pages;
    ptest_assert_msg(Prusage(usage) == 0,"initial mapping accounting");
    baseline_pages = usage[4];
    for (cycle = 0; cycle < 3; cycle++) {
        long memory = Malloc(RUNNABLE_STACK_SIZE);
        ptest_assert_msg(memory > 0,"allocate private clone stack");
        if (memory <= 0) return;
        runnable_stacks[cycle] = (unsigned char *)memory;
    }
#if defined(__mc68000__)
    if (Ssystem(S_GETCOOKIE,0x5f465055L,(long)&has_fpu) != 0) has_fpu = 0;
#else
    has_fpu = 0;
#endif
    probe.parent = Pgetpid();
    for (cycle = 0; cycle < 3; cycle++) for (mode = 0; mode < 3; mode++) {
        probe.steps = probe.error = 0; probe.mode = mode;
        pid = tid = -99;
#if defined(__mc68000__)
        /* The return/argument frame straddles discontiguous pages and has
         * native two-byte alignment, including its 32-bit argument. */
        stack_trim = cycle == 2 && mode == 0 ? 4094 : 0;
#endif
        result = launch_probe(CLONE_VM, 0, &pid, &tid);
        if (result <= 0) {
            char msg[64];
            sprintf(msg, "native clone creation returned %ld\r\n", result);
            conws(msg);
            ptest_assert_msg(0,"native clone creation failed");
            return;
        }
        ptest_assert_msg(result > 0 && result == tid && pid > 0 && pid != probe.parent,
                         "native NEW_PROCESS must publish distinct PID and positive TID");
        ptest_assert_msg(probe.steps == 0,"child dispatched before Pclone returned");
        waited = Pwaitpid(pid,0,0);
        ptest_assert_msg((waited >> 16) == pid,"blocked process wait must dispatch and reap clone");
        ptest_assert_msg((waited & 65535) == (mode == 1 ? 23 : mode == 2 ? 11*256 : 0),
                         "normal/Pterm/fault clone status");
        if (probe.error) {
            char message[64];
            sprintf(message,"clone register error mask %ld\r\n",probe.error); conws(message);
#if defined(__mc68000__)
            sprintf(message,"FPIAR before/after %lx/%lx\r\n",clone_fpu_expected_iar,clone_fpu_observed); conws(message);
#endif
        }
        ptest_assert_msg(probe.pid == pid && probe.steps == 12 && !probe.error,
                         "native entry argument, shared memory and register/stack continuation");
        ptest_assert_msg(probe.local >= (unsigned long)runnable_stacks[0] &&
                         probe.local < (unsigned long)(runnable_stacks[0]+RUNNABLE_STACK_SIZE),
                         "clone executed on its independent user stack");
        ptest_assert_msg(parent_local == 0x2468ace0UL && Pgetpid() == probe.parent,
                         "child exit corrupted parent context");
    }
    probe.steps = probe.error = 0; probe.mode = 0;
    result = launch_probe(CLONE_VM|CLONE_SAME_PROCESS,1,&pid,&tid);
    ptest_assert_msg(result == tid && tid > 0 && pid == probe.parent,
                     "native sibling must share PID");
    result = launch_probe(CLONE_VM|CLONE_SAME_PROCESS,2,&pid,&other_tid);
    ptest_assert_msg(result == other_tid && other_tid > 0 && other_tid != tid,
                     "native siblings must have independent TIDs");
    for (cycle = 0; cycle < 128 && probe.steps < 24; cycle++) (void)Syield();
    ptest_assert_msg(probe.steps == 24,"native siblings did not progress");
    /* Each increments before its final yield; let both return and root reap. */
    (void)Syield(); (void)Syield();
    ptest_assert_msg(!probe.error && probe.pid == probe.parent,"shared-PROCESS siblings must retain resources");
    stack_trim = 0;
    {
        struct {
            struct ptos_clone_args args;
            unsigned long unknown_tail;
        } extended = {{0},0xffffffffUL};
        int size;
        extended.args.flags = CLONE_VM;
        extended.args.entry = runnable_entry;
        extended.args.argument = (void *)&probe;
        extended.args.stack_base = runnable_stacks[0];
        extended.args.stack_size = RUNNABLE_STACK_SIZE;
        extended.args.child_tid = &tid; extended.args.child_pid = &pid;
        for (size = 28; size <= 36; size += 8) {
            probe.steps = probe.error = 0; probe.mode = 0;
            pid = tid = -99;
            result = pclone_trap(&extended,size);
            ptest_assert_msg(result > 0 && tid == result && (size == 28 ? pid == -99 : pid > 0),
                             "native prefix and append-only publication contract");
            if (result > 0) {
                waited = Pwaitpid(size == 28 ? -1 : pid,0,0);
                ptest_assert_msg(waited > 0 && !(waited & 65535) && probe.steps == 12 && !probe.error,
                                 "append-only clone executes and waits");
            }
        }
    }
#if defined(TEST_WITH_PTHJOIN)
    test_joins();
#endif
    test_lifecycle();
    for (cycle = 0; cycle < 3; cycle++)
        ptest_assert_msg(Mfree(runnable_stacks[cycle]) == 0,"clone stack hold released after root reap");
    ptest_assert_msg(Prusage(usage) == 0 && usage[4] == baseline_pages,
                     "native mapped-page accounting returns to baseline");
}
void pclone_run_managed_tests(void);
void pclone_run_managed_tests(void)
{
    ptest_begin("pclone managed execution");
    test_runnable();
    ptest_pass();
#if defined(__mc68000__)
    if (has_fpu && clone_fpu_expected_iar != 0x12345678UL)
        conws("m68k FPIAR write/read is not modeled; observable value preserved\r\n");
#endif
    if (!ptest_failures()) {
        conws("pclone native runnable NEW_PROCESS/SAME_PROCESS: PASS\r\n");
#if defined(TEST_WITH_PTHJOIN)
        conws("native Pthjoin: PASS\r\n");
#endif
    }
}
#endif
void test_pclone(void);
void test_pclone(void)
{
    ptest_begin("pclone");
#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_USER_ASPACE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
    {
        static unsigned char stack[256] __attribute__((aligned(16)));
        int32_t tid = 0x12345678;
        struct ptos_clone_args args = { 0 };
        args.entry = child_entry;
        args.stack_base = stack;
        args.stack_size = sizeof(stack);
        args.child_tid = &tid;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -32L,"valid request must reach unavailable backend");
        ptest_assert_msg(pclone_trap(0,sizeof(args)) == -40L,"null structure must fail");
        ptest_assert_msg(pclone_trap((void *)0xffff0000UL,sizeof(args)) == -40L,"inaccessible structure must fail");
        ptest_assert_msg(pclone_trap(&args,PTOS_CLONE_ARGS_MIN_SIZE-1) == -25L,"short structure must fail");
        ptest_assert_msg(pclone_trap((void *)0x80001000UL,sizeof(args)) == -40L,"high-bit pointer must not be truncated");
        ptest_assert_msg(pclone_trap(&args,0xffffffffUL) == -40L,"negative-looking size must not wrap");
#ifndef TEST_WITH_NATIVE_SCHED
        args.flags = CLONE_VM;
        ptest_assert_msg(pclone_trap(&args,28) == -32L,"shared VM backend remains unavailable on this architecture");
        args.flags = CLONE_VM | CLONE_SAME_PROCESS;
        ptest_assert_msg(pclone_trap(&args,32) == -32L,"same-process startup remains unavailable");
#endif
        args.flags = 0x80000000U;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -25L,"reserved flag must fail");
        args.flags = 0; args.entry = (void (*)(void *))0xffff0000UL;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -40L,"inaccessible entry must fail");
        args.entry = child_entry; args.child_tid = (int32_t *)0xffff0000UL;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -40L,"unwritable output must fail");
        ptest_assert_msg(tid == 0x12345678,"failure must not publish a child TID");
        ptest_assert_msg(entry_ran == 0,"no child entry may run");
    }
#endif
#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
    {
        long child, status;
        /* The shell may itself own the group. A nested synchronous Pexec is
         * intentionally a launcher continuation, not a second managed task.
         * Use the standard async launch to test Pclone's managed contract. */
        static const char tail[] = "\7--clone";
        child = Pexec(100,"runtests.tos",tail,0);
        ptest_assert_msg(child > 0,"managed clone test worker must start");
        if (child > 0) {
            status = Pwaitpid(child,0,0);
            ptest_assert_msg((status >> 16) == child && !(status & 65535),
                             "managed clone execution worker must pass");
        }
    }
#endif
    ptest_pass();
}
