/* User-mode Pclone checks through the real GEMDOS trap, not kernel calls. */
#include "test.h"
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#include <mint/osbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>

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
#if defined(TEST_WITH_PTERMGRP)
static const char group_env[2]={0,0};
static struct {
    volatile long mode, ready, requested, error, returned, peer_done, peer_go, child_go, child_done, atexit_calls;
    volatile long session, window, wait_done;
    int32_t target, joiner, killer, nested, wait_child;
    unsigned char *stack[6];
} group;
static long term_group(short status) { return trap_1_ww(GEMDOS_PTERMGRP,status); }
static void group_mark(const char *name)
{
    char path[32];
    long h;
    sprintf(path,"C:\\%s",name); h=Fcreate(path,0);
    if (h>=0) (void)Fclose(h);
}
static int group_exists(const char *name)
{
    char path[32];
    long h;
    sprintf(path,"C:\\%s",name); h=Fopen(path,0);
    if (h<0) return 0;
    (void)Fclose(h); return 1;
}
#if defined(__x86_64__) && defined(TEST_WITH_CUIS)
static long native_cuis(unsigned fn, long handle, unsigned long long value)
{
    register unsigned long long ax __asm__("rax")=(0x103ULL<<32)|fn;
    register unsigned long long di __asm__("rdi")=(unsigned long)handle;
    register unsigned long long si __asm__("rsi")=value;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di), "r"(si) : "rcx","r11","memory","cc");
    return (long)ax;
}
static long native_cuis5(unsigned fn, long a, long b, long c, long d, long e)
{
    register unsigned long long ax __asm__("rax")=(0x103ULL<<32)|fn;
    register unsigned long long di __asm__("rdi")=(long long)a;
    register unsigned long long si __asm__("rsi")=(long long)b;
    register unsigned long long dx __asm__("rdx")=(long long)c;
    register unsigned long long r10 __asm__("r10")=(long long)d;
    register unsigned long long r8 __asm__("r8")=(long long)e;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di),"r"(si),"r"(dx),"r"(r10),"r"(r8) : "rcx","r11","memory","cc");
    return (long)ax;
}

#if defined(TEST_WITH_PTHJOIN)
static long native_cuis6(unsigned fn, unsigned long long a, unsigned long long b,
                         unsigned long long c, unsigned long long d,
                         unsigned long long e, unsigned long long f)
{
    register unsigned long long ax __asm__("rax")=(0x103ULL<<32)|fn;
    register unsigned long long di __asm__("rdi")=a, si __asm__("rsi")=b;
    register unsigned long long dx __asm__("rdx")=c, r10 __asm__("r10")=d;
    register unsigned long long r8 __asm__("r8")=e, r9 __asm__("r9")=f;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di),"r"(si),"r"(dx),"r"(r10),"r"(r8),"r"(r9) : "rcx","r11","memory","cc");
    return (long)ax;
}
static struct { volatile long error, first_tick, second_tick; long session, child; } native_resume;
static void native_resume_peer(void *argument)
{
    static const char path[]="C:\\GROUPCH.TOS", tail[]={1,'3',0};
    long x,y,buttons,modifiers,token;
    (void)argument;
    /* Announce fresh input only after the leader has admitted its wait. */
    conws("CUIS resume first: READY\r\n");
    do {
        if (native_cuis5(33,native_resume.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) {
            native_resume.error=1; return;
        }
        if (!(buttons&1)) (void)Syield();
    } while (!(buttons&1));
    native_resume.first_tick=Ssystem(S_GETLVAL,0x4baL,0L);
    token=native_cuis5(66,native_resume.session,0,0,0,0);
    if (token<=0) { native_resume.error=1; return; }
    native_resume.child=native_cuis6(69,native_resume.session,token,(unsigned long)path,(unsigned long)tail,0,0);
    if (native_resume.child<=0) { native_resume.error=1; return; }
    do {
        if (native_cuis5(33,native_resume.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) { native_resume.error=1; return; }
        if (buttons&1) (void)Syield();
    } while (buttons&1);
    do {
        if (native_cuis5(33,native_resume.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) { native_resume.error=1; return; }
        if (!(buttons&1)) (void)Syield();
    } while (!(buttons&1));
    native_resume.second_tick=Ssystem(S_GETLVAL,0x4baL,0L);
    conws("CUIS resume second: READY\r\n");
}
static struct {
    volatile long mode, ready, go, error, replacement, requested, returned;
    long session, peer_session, parent_pid, parent_client, peer_client;
    unsigned char *data, *output, *late;
} native_stream;
static long stream_call(unsigned fn,long session,void *data,long length,long block,long *sender)
{
    return native_cuis6(fn,(unsigned long)session,(unsigned long)data,(unsigned long)length,
                        (unsigned long)block,(unsigned long)sender,0);
}
static long stream_client_id(long session)
{
    unsigned long identity[4]={0,0,0,0};
    if (native_cuis6(76,session,session,5,(unsigned long)identity,sizeof(identity),0)!=16) return -1;
    return (long)identity[0];
}
static void stream_native_peer(void *argument)
{
    long sender=-1,rc;
    (void)argument;
    if (native_stream.mode==0) {
        if (stream_call(30,native_stream.session,native_stream.data,1,0,0)!=-36L ||
            stream_call(31,native_stream.session,native_stream.output,16,0,&sender)!=16 ||
            sender!=native_stream.parent_client || native_stream.output[0]!=0x35) native_stream.error=1;
        native_stream.data[0]=99;
    } else if (native_stream.mode==1) {
        long own=native_cuis(0,0,0);
        native_stream.peer_client=stream_client_id(own);
        if (own<0 || native_stream.peer_client<=0 || stream_call(30,native_stream.session,native_stream.data,16,0,0)!=16) native_stream.error=1;
    } else if (native_stream.mode==2 || native_stream.mode==3 || native_stream.mode==6) {
        native_stream.peer_session=native_cuis(0,0,0);
        if (native_stream.peer_session<0) native_stream.error=1;
        native_stream.ready=1;
        while (!native_stream.go) (void)Syield();
        if (native_stream.mode==6) return; /* PROCESS exit retires the retained receiver. */
        if (native_stream.mode==3) {
            if (native_cuis(2,native_stream.peer_session,0)) native_stream.error=1;
            native_stream.replacement=native_cuis(0,0,0);
            if (native_stream.replacement<=native_stream.peer_session ||
                stream_call(89,native_stream.replacement,0,0,0,0)!=0) native_stream.error=1;
            native_stream.ready=2;
        }
        while (native_stream.go!=2) (void)Syield();
        rc=stream_call(89,native_stream.mode==3?native_stream.replacement:native_stream.peer_session,0,0,0,0);
        if (rc!=(native_stream.mode==3?0:65536L)) native_stream.error=1;
        if (native_stream.mode==2 &&
            (stream_call(31,native_stream.peer_session,native_stream.output,4,0,&sender)!=4 ||
             sender!=native_stream.parent_client)) native_stream.error=1;
    } else if (native_stream.mode==4) {
        if (stream_call(31,native_stream.session,native_stream.output,1,0,0)!=-36L ||
            stream_call(88,native_stream.session,native_stream.output,1,0,0)!=-36L) native_stream.error=1;
        if (native_cuis(2,native_stream.session,0)) native_stream.error=1;
        native_stream.replacement=native_cuis(0,0,0);
        if (native_stream.replacement<=native_stream.session ||
            stream_call(89,native_stream.replacement,0,0,0,0)!=0) native_stream.error=1;
    } else if (native_stream.mode==5) {
        if (stream_call(31,native_stream.session,native_stream.output,1,0,0)!=-36L ||
            Mfree(native_stream.late)!=0) native_stream.error=1;
        native_stream.late=0;
        if (stream_call(30,native_stream.session,native_stream.data,16,0,0)!=16) native_stream.error=1;
    }
}
static void stream_native_retire_writer(void *argument)
{
    (void)argument;
    if (stream_call(30,native_stream.peer_session,native_stream.data,1,0,0)!=-36L ||
        native_cuis(2,native_stream.session,0)) native_stream.error=1;
    native_stream.replacement=native_cuis(0,0,0);
    if (native_stream.replacement<=native_stream.session) native_stream.error=1;
}
static void stream_native_killer(void *argument)
{
    (void)argument; native_stream.requested=1;
    (void)term_group(73);
    native_stream.error=1;
}
static void stream_native_group(void *argument)
{
    int32_t pid,tid;
    long own=native_cuis(0,0,0);
    native_stream.peer_client=stream_client_id(own);
    (void)argument;
    if (own<0 || stream_call(30,native_stream.session,native_stream.data,32767,0,0)!=32767 ||
        stream_call(30,native_stream.session,native_stream.data,32767,0,0)!=32767 ||
        stream_call(30,native_stream.session,native_stream.data,2,0,0)!=2 ||
        launch_entry(stream_native_killer,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],&pid,&tid)<=0) {
        native_stream.error=1; return;
    }
    if (stream_call(30,native_stream.session,native_stream.data,16,1,0)!=16) native_stream.error=1;
    native_stream.returned=1; /* Raw stream wait completes; cancellation is still deferred. */
    (void)Syield(); /* Safe ordinary checkpoint after all stream resources are gone. */
    native_stream.error=1;
}
static void test_native_streams(void)
{
    int mode,spin;
    int32_t pid,tid,retire_pid,retire_tid,status;
    long rc,sender,own,original=Pgetpid();
    native_stream.data=(void *)Malloc(32767); native_stream.output=(void *)Malloc(32767);
    ptest_assert_msg((long)native_stream.data>0 && (long)native_stream.output>0,"stream fixture buffers");
    if ((long)native_stream.data<=0 || (long)native_stream.output<=0) {
        if ((long)native_stream.data>0) (void)Mfree(native_stream.data);
        if ((long)native_stream.output>0) (void)Mfree(native_stream.output);
        return;
    }
    native_stream.parent_pid=original;
    for (mode=0;mode<7;mode++) {
        native_stream.mode=mode; native_stream.ready=native_stream.go=native_stream.error=0;
        native_stream.replacement=-1; retire_tid=-1;
        memset(native_stream.data,0x35,32767);
        own=native_stream.session=native_cuis(0,0,0); native_stream.parent_client=stream_client_id(own);
        ptest_assert_msg(own>=0,"native stream session");
        if (own<0) break;
        if (mode==0) {
            ptest_assert_msg(stream_call(30,own,native_stream.data,32767,0,0)==32767 &&
                stream_call(30,own,native_stream.data,32767,0,0)==32767 &&
                stream_call(30,own,native_stream.data,2,0,0)==2,"fill loopback destination");
        }
        if (mode==5) native_stream.late=(void *)Malloc(4096);
        rc=launch_entry(stream_native_peer,CLONE_VM|((mode==1||mode==2||mode==3||mode==6)?0:CLONE_SAME_PROCESS),
                        runnable_stacks[0],&pid,&tid);
        ptest_assert_msg(rc>0,"stream peer constructed");
        if (rc<=0) break;
        if (mode==2 || mode==3 || mode==6) {
            for (spin=0;spin<2000 && !native_stream.ready;spin++) (void)Syield();
            ptest_assert_msg(native_stream.ready && !native_stream.error,"separate PROCESS stream receiver");
            ptest_assert_msg(stream_call(30,native_stream.peer_session,native_stream.data,32767,0,0)==32767 &&
                stream_call(30,native_stream.peer_session,native_stream.data,32767,0,0)==32767 &&
                stream_call(30,native_stream.peer_session,native_stream.data,2,0,0)==2,"fill foreign destination");
            if (mode==2) ptest_assert_msg(launch_entry(stream_native_retire_writer,CLONE_VM|CLONE_SAME_PROCESS,
                runnable_stacks[2],&retire_pid,&retire_tid)>0,"source-retirement sibling");
            native_stream.go=1;
            ptest_assert_msg(stream_call(30,native_stream.peer_session,native_stream.data,16,1,0)==-36L,
                             "blocked WRITE wakes on source or receiver retirement");
            if (mode==2) {
                ptest_assert_msg(join_tid(retire_tid,&status)==0 && status==0,"retirement sibling reaped");
                own=native_stream.replacement;
            } else ptest_assert_msg(stream_call(30,native_stream.peer_session,native_stream.data,16,0,0)==-36L,
                                   "old receiver handle cannot target replacement");
            native_stream.go=2;
        } else if (mode==0) {
            ptest_assert_msg(stream_call(30,own,native_stream.data,16,1,0)==16,"blocked writer progresses with independent reader slot");
        } else {
            rc=stream_call(31,own,mode==5?native_stream.late:native_stream.output,mode==1?8:16,1,&sender);
            ptest_assert_msg(rc==(mode==4?-36L:mode==5?-40L:8L),"READ retirement or resident-only late copy failure");
            if (mode==4) own=native_stream.replacement;
            if (mode==1) ptest_assert_msg(sender==native_stream.peer_client && native_stream.output[0]==0x35,"historical foreign sender and partial data");
            if (mode==5) ptest_assert_msg(stream_call(31,own,native_stream.output,16,0,&sender)==16 &&
                sender==native_stream.parent_client && native_stream.output[0]==0x35,"failed read leaves all bytes available");
        }
        if (mode==1 || mode==2 || mode==3 || mode==6) ptest_assert_msg((Pwaitpid(pid,0,0)&65535)==0,"stream PROCESS reaped");
        else ptest_assert_msg(join_tid(tid,&status)==0 && status==0,"stream THREAD reaped");
        ptest_assert_msg(!native_stream.error && Pgetpid()==original,"stream identities and independent parent survive");
        if (mode==1) {
            ptest_assert_msg(stream_call(88,own,native_stream.output,4,0,&sender)==4 && sender==native_stream.peer_client &&
                stream_call(89,own,0,0,0,0)==8 && stream_call(31,own,native_stream.output,8,0,&sender)==8 && sender==native_stream.peer_client,
                "PEEK preserves bytes and READ preserves sender after source exit");
        }
        if (mode==0) {
            ptest_assert_msg(stream_call(31,own,native_stream.output,32767,0,0)==32767 &&
                stream_call(31,own,native_stream.output,32767,0,0)==32767 && native_stream.output[32753]==0x35 &&
                stream_call(31,own,native_stream.output,2,0,0)==2 && native_stream.output[0]==0x35,
                "blocked WRITE owns its immutable copyin snapshot through wrap");
        }
        ptest_assert_msg(native_cuis(2,own,0)==0,"native stream session teardown");
    }
    native_stream.session=native_cuis(0,0,0); native_stream.error=native_stream.requested=native_stream.returned=0;
    memset(native_stream.data,0x35,32767);
    rc=launch_entry(stream_native_group,CLONE_VM,runnable_stacks[1],&pid,&tid);
    ptest_assert_msg(rc>0,"pending-stream group constructed");
    if (rc>0) {
        for (spin=0;spin<2000 && !native_stream.requested;spin++) (void)Syield();
        ptest_assert_msg(native_stream.requested && !native_stream.error,"group request during admitted raw stream wait");
        for (spin=0;spin<16;spin++) {
            ptest_assert_msg(Pwaitpid(pid,1,0)==0 && !native_stream.returned,"noncancellable stream retains group until ordinary progress");
            (void)Syield();
        }
        ptest_assert_msg(stream_call(31,native_stream.session,native_stream.output,16,0,&sender)==16 && sender==native_stream.peer_client,
                         "independent PROCESS reader makes progress");
        rc=Pwaitpid(pid,0,0);
        ptest_assert_msg((rc>>16)==pid && (rc&65535)==73 && native_stream.returned && !native_stream.error,
                         "group exits at later safe checkpoint with committed stream intact");
        ptest_assert_msg(Pwaitpid(pid,1,0)==-33L && stream_call(89,native_stream.session,0,0,0,0)==65536L,
                         "one PROCESS completion, no retraction of committed packet");
    }
    ptest_assert_msg(native_cuis(2,native_stream.session,0)==0 && Mfree(native_stream.data)==0 &&
                     Mfree(native_stream.output)==0,"stream buffers and GUI resources reclaimed");
    /* Whole managed worker status/baseline is checked by the parent suite;
     * do not print a success verdict before those remaining assertions. */
    conws("native CUIS retained stream cases: exercised\r\n");
}
static struct {
    volatile long session, mode, adopt, error, returned, first_tick, wait_result, initial_count;
} native_cancel;
static void native_cancel_peer(void *argument)
{
    static const char path[]="C:\\GROUPCH.TOS", tail[]={1,'3',0};
    long x,y,buttons=0,modifiers,token,launched;
    (void)argument;
    /* Pclone publishes without dispatching. With preemption disabled this
     * peer first runs after the leader admits SESSION_WAIT and blocks. An
     * earlier marker permits a legal initial held-button quick-check result
     * before this peer has requested cancellation. */
    conws(native_cancel.mode?"CUIS cancel resume first: READY\r\n":"CUIS cancel button: READY\r\n");
    do {
        if (native_cuis5(33,native_cancel.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) {
            native_cancel.error=1; return;
        }
        if (!(buttons&1)) (void)Syield();
    } while (!(buttons&1));
    native_cancel.first_tick=Ssystem(S_GETLVAL,0x4baL,0L);
    if (native_cancel.mode) {
        token=native_cuis5(66,native_cancel.session,0,0,0,0);
        launched=token>0?native_cuis6(69,native_cancel.session,token,(unsigned long)path,(unsigned long)tail,(unsigned long)group_env,0):token;
        if (launched<=0) {
            native_cancel.error=launched?launched:1;
            printf("resume observer launch token=%ld result=%ld\r\n",token,launched); return;
        }
        while (!native_cancel.adopt) (void)Syield();
        /* The leader publishes adopt and enters SESSION_WAIT without yielding.
         * This peer can run again only after that resumed wait blocks. Gate
         * the second physical press here, not at the leader's pre-wait flag. */
        conws("CUIS cancel resume admitted: READY\r\n");
        do {
            if (native_cuis5(33,native_cancel.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) {
                native_cancel.error=1; return;
            }
            if (!(buttons&1)) (void)Syield();
        } while (!(buttons&1));
    }
    (void)term_group((short)(71+native_cancel.mode));
    native_cancel.returned=2;
}
static void native_cancel_leader(void *argument)
{
    int32_t pid,tid;
    long x,y,buttons=0,modifiers,result;
    (void)argument;
    native_cancel.session=native_cuis(0,0,0);
    if (native_cancel.session<0 || native_cuis5(47,native_cancel.session,1,1,0,0) ||
        native_cuis6(44,native_cancel.session,1,1,2,0,0) ||
        native_cuis6(46,native_cancel.session,3000,1,0,0,0)!=3000) { native_cancel.error=1; return; }
    /* Wait for the previous fixture's held release before announcing a fresh
     * gesture; the host never relies on monitor ACK as an IRQ-delivery fence. */
    do {
        if (native_cuis5(33,native_cancel.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) {
            native_cancel.error=1; return;
        }
        if (buttons&1) (void)Syield();
    } while (buttons&1);
    if (native_cancel.mode < 0) {
        long values[5]={0};
        /* Reproduce the old host/leader gap deliberately. The physical press
         * precedes admission, so the initial quick check must return a result
         * rather than waiting for a cancellation that has not been requested. */
        conws("CUIS initial button: READY\r\n");
        do {
            if (native_cuis5(33,native_cancel.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) {
                native_cancel.error=1; return;
            }
        } while (!(buttons&1));
        native_cancel.wait_result=native_cuis5(32,native_cancel.session,0,16|2,5000,0);
        if (native_cuis6(45,native_cancel.session,(unsigned long)&values[0],(unsigned long)&values[1],
                         (unsigned long)&values[2],(unsigned long)&values[3],(unsigned long)&values[4]))
            native_cancel.error=1;
        native_cancel.initial_count=values[1];
        native_cancel.returned=1;
        return;
    }
    if (launch_entry(native_cancel_peer,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[2],&pid,&tid)<=0) {
        native_cancel.error=1; return;
    }
    result=native_cuis5(32,native_cancel.session,0,16|2|(native_cancel.mode?2048:0),5000,0);
    if (native_cancel.mode && result==2048) {
        conws("CUIS cancel resume checkpoint: READY\r\n");
        do {
            if (native_cuis5(33,native_cancel.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers)) {
                native_cancel.error=1; return;
            }
            if (buttons&1) (void)Syield();
        } while (buttons&1);
        native_cancel.adopt=1;
        (void)native_cuis5(32,native_cancel.session,0,16|2,5000,0);
    }
    native_cancel.wait_result=result;
    native_cancel.returned=1; /* Cancellation must not return to this user continuation. */
}
static void test_native_input_cancel(void)
{
    int mode,ok=1;
    int32_t pid,tid;
    long result;
    native_cancel.mode=-1;
    native_cancel.error=native_cancel.returned=native_cancel.wait_result=native_cancel.initial_count=0;
    result=launch_entry(native_cancel_leader,CLONE_VM|CLONE_COPY_STANDARD_HANDLES|CLONE_COPY_CWD,
                        runnable_stacks[1],&pid,&tid);
    ptest_assert_msg(result>0,"initial-button PROCESS constructed");
    if (result<=0) return;
    result=Pwaitpid(pid,0,0);
    ok=(result>>16)==pid && !(result&65535) && !native_cancel.error && native_cancel.returned==1 &&
        native_cancel.wait_result==16 && native_cancel.initial_count==1;
    ptest_assert_msg(ok,"physical input before admission completes the initial quick check");
    if (!ok) return;
    conws("native CUIS initial button before admission: PASS\r\n");
    for (mode=0;mode<2;mode++) {
        native_cancel.mode=mode; native_cancel.error=native_cancel.returned=native_cancel.adopt=0;
        native_cancel.first_tick=native_cancel.wait_result=0;
        result=launch_entry(native_cancel_leader,CLONE_VM|CLONE_COPY_STANDARD_HANDLES|CLONE_COPY_CWD,
                            runnable_stacks[1],&pid,&tid);
        ptest_assert_msg(result>0,"native input cancellation PROCESS constructed");
        if (result<=0) return;
        result=Pwaitpid(pid,0,0);
        if ((result>>16)!=pid || (result&65535)!=71+mode || native_cancel.error || native_cancel.returned ||
            !native_cancel.first_tick ||
            (unsigned long)(Ssystem(S_GETLVAL,0x4baL,0L)-native_cancel.first_tick)>=600) ok=0;
        if (!ok) printf("input cancellation mode=%d wait=%lx error=%ld returned=%ld result=%ld first=%ld now=%ld\r\n",mode,result,native_cancel.error,native_cancel.returned,native_cancel.wait_result,native_cancel.first_tick,Ssystem(S_GETLVAL,0x4baL,0L));
        ptest_assert_msg(ok,"tracked/resumed button cancellation completes before click deadline with exact group status");
        ptest_assert_msg(Pwaitpid(pid,1,0)==-33L,"cancelled input PROCESS completion consumed once");
        if (ok) conws(mode?"native CUIS resumed button cancellation: PASS\r\n":
                          "native CUIS tracked button cancellation: PASS\r\n");
    }
    if (ok) conws("native CUIS history cancellation: PASS\r\n");
}
static void test_native_resume(void)
{
    long window=-1, interval, result, values[5]={0}, x,y,buttons=0,modifiers;
    int32_t pid,tid,status;
    int ok=1;
    long marker=Fopen("C:\\HISTGUI.ON",0);
    if (marker<0) return; /* dedicated GUI run; console leases suppress GUI input */
    (void)Fclose(marker);
#define RESUME_CHECK(condition, message) do { int valid=(condition); if (!valid) ok=0; ptest_assert_msg(valid,message); } while (0)
    native_resume.error=0; native_resume.child=0; native_resume.first_tick=native_resume.second_tick=0;
    native_resume.session=native_cuis(0,0,0);
    RESUME_CHECK(native_resume.session>=0,"native resume session");
    if (native_resume.session<0) return;
    interval=native_cuis6(46,native_resume.session,0,0,0,0,0);
    window=native_cuis5(3,0,0,300,300,0);
    RESUME_CHECK(window>=0 && native_cuis(38,window,1)==0 && native_cuis(6,window,0)==0,
                     "native resume focused work area");
    RESUME_CHECK(native_cuis6(46,native_resume.session,3000,1,0,0,0)==3000 &&
                     native_cuis6(44,native_resume.session,1,1,2,0,0)==0,"native double-click policy");
    RESUME_CHECK(native_cuis5(47,native_resume.session,1,0,0,0)==0,"native modal pointer ownership");
    result=launch_entry(native_resume_peer,CLONE_VM|CLONE_SAME_PROCESS,runnable_stacks[0],&pid,&tid);
    RESUME_CHECK(result>0,"native SAME_PROCESS child observer peer");
    if (result<=0) goto out;
    result=native_cuis5(32,native_resume.session,0,16|2048|2,5000,0);
    RESUME_CHECK(result==2048 && !native_resume.error,"child wake checkpoints first click before deadline");
    if (result!=2048 || native_resume.error) goto joined;
    conws("CUIS resume checkpoint: READY\r\n");
    RESUME_CHECK(native_cuis5(32,native_resume.session,0,16|2,0,0)==2,
                     "zero timer commits resume ownership without restarting clicks");
    do {
        result=native_cuis5(33,native_resume.session,(long)&x,(long)&y,(long)&buttons,(long)&modifiers);
        if (buttons&1) (void)Syield();
    } while (!result && (buttons&1));
    RESUME_CHECK(result==0,"first hardware release observed");
    conws("CUIS resume adopt: READY\r\n");
    result=native_cuis5(32,native_resume.session,0,16|2,5000,0);
    if (result!=16) printf("native resume completion=%ld interval=%ld\r\n",result,native_cuis6(46,native_resume.session,0,0,0,0,0));
    RESUME_CHECK(result==16,"resumed hardware click sequence completes as button input");
    result=native_cuis6(45,native_resume.session,(unsigned long)&values[0],(unsigned long)&values[1],
                     (unsigned long)&values[2],(unsigned long)&values[3],(unsigned long)&values[4]);
    if (result || values[1]!=2 || (values[4]&3)!=1)
        printf("native resume snapshot rc=%ld count=%ld buttons=%ld xy=%ld,%ld ticks=%ld,%ld,%ld\r\n",result,values[1],values[4],values[2],values[3],native_resume.first_tick,native_resume.second_tick,Ssystem(S_GETLVAL,0x4baL,0L));
    RESUME_CHECK(result==0 && values[1]==2 && (values[4]&3)==1,"resumed click count is exactly two");
    RESUME_CHECK(native_cuis6(44,native_resume.session,1,1,1,0,0)==0 &&
                     native_cuis5(32,native_resume.session,0,16|2,0,0)==2,
                     "resume/live cache cannot redeliver committed clicks");
joined:
    /* Reap through the existing TID and PROCESS consumers; CUIS merely
     * retains its independent completion notice until session teardown. */
    if (result!=16 && native_resume.child<=0) (void)native_cuis(2,native_resume.session,0);
    RESUME_CHECK(join_tid(tid,&status)==0 && status==0,"native observer peer reaped");
    if (native_resume.child>0) {
        result=Pwaitpid(native_resume.child,0,0);
        RESUME_CHECK((result>>16)==native_resume.child && (result&65535)==92,"native observed child exits once");
        RESUME_CHECK((native_cuis5(67,native_resume.session,0,0,0,0)&65535)==92,"CUIS notice survives PROCESS wait");
    }
out:
    (void)native_cuis5(47,native_resume.session,0,0,0,0);
    if (ok) {
        conws("native CUIS resume/live commit: PASS\r\n");
        test_native_input_cancel();
    }
    (void)native_cuis6(46,native_resume.session,interval,1,0,0,0);
    (void)native_cuis(2,native_resume.session,0);
}
#undef RESUME_CHECK

#endif

#endif
static void group_nested(void *arg);
static void group_target(void *arg)
{
    (void)arg; group.ready|=1;
#if defined(__x86_64__) && defined(TEST_WITH_CUIS)
    if (group.mode>=5) {
        long rc;
        group.session=native_cuis(0,0,0);
        if (group.session<0) group.error=1;
        if (group.mode>=10) rc=native_cuis5(32,group.session,0,group.mode==13?16:group.mode==10?8:10,group.mode==12?100:0,0);
        else if (group.mode==5) rc=native_cuis(24,group.session,3600000UL);
        else if (group.mode==7 || group.mode==8) {
            group.window=native_cuis5(3,1,1,32,32,0);
            if (group.window<0 || native_cuis(6,group.window,0)) group.error=1;
            rc=native_cuis5(20,group.window,16,-1,0,0); /* retained window/control wait */
        } else rc=native_cuis5(32,group.session,1,1,0,0); /* pending stream readiness */
        if (group.mode==12 && rc==2) {
            group.wait_done=1; /* rejected legacy outcome: pending cancellation must win */
            (void)native_cuis5(32,group.session,0,8,0,0);
            group.returned=1; /* pending cancellation must stop before new admission */
        }
        else if ((group.mode==8 || group.mode==9 || group.mode==10 || group.mode==11) && rc==-36L) group.wait_done=1;
        else group.returned=1; /* cancellation must not return to user */
    }
#endif
    if (group.mode==1 || group.mode==2) group_nested(arg);
    for (;;) (void)Syield();
}
static void group_joiner(void *arg)
{
    (void)arg; group.ready|=2;
#if defined(__x86_64__) && defined(TEST_WITH_CUIS)
    if (group.mode==12) {
        *(volatile long *)0=1; /* sibling fault cannot abandon the active sampler */
        group.error=1;
    }
#endif
#if defined(TEST_WITH_PTHJOIN)
    if (join_tid(group.target,0)==0) group.error=1;
    group.returned=1;
#else
    for (;;) (void)Syield();
#endif
}
static void group_nested(void *arg)
{
    static const char tail[]="\1" "2";
    (void)arg;
    { long rc=Pexec(0,"C:\\GROUPCH.TOS",tail,(void *)group_env); if (rc!=19) group.error=rc; }
    /* Group cancellation must intercept GEMDOS return before this executes. */
    group.returned=1;
}
static void group_atexit(void)
{
    group.atexit_calls++;
    /* Real GEMDOS from the cleanup handler before PROCESS termination. */
    (void)Pgetpid();
    (void)Syield();
}
static void group_killer(void *arg)
{
    (void)arg;
    while ((group.ready&3)!=3 || ((group.mode==1 || group.mode==2) && !group_exists("GDEEP.ON"))) (void)Syield();
    if (!group.mode) { for (;;) (void)Syield(); }
#if defined(__x86_64__) && defined(TEST_WITH_CUIS)
    if (group.mode==8 || group.mode==9 || group.mode==10 || group.mode==11) {
        if (group.mode>=10 && native_cuis5(32,group.session,0,10,0,0)!=-36L) group.error=1;
        if (group.mode==8) {
            if (native_cuis(5,group.window,0)) group.error=1;
        } else {
            long replacement;
            if (native_cuis(2,group.session,0)) group.error=1;
            replacement=native_cuis(0,0,0);
            if (replacement<=group.session) group.error=1;
            if (group.mode>=10 && native_cuis5(32,replacement,0,2,0,0)!=2) group.error=1;
        }
        while (!group.wait_done && !group.returned) (void)Syield();
    }
#endif
    group.requested=1;
    if (group.mode==4) {
        if (atexit(group_atexit)) group.error=1;
        exit(62); /* real libc entry: callbacks then Ptermgrp, never raw Pterm */
    }
    (void)term_group(61);
    group.returned=1;
}
static void group_peer(void *arg)
{
    (void)arg;
    while (!group.peer_go) (void)Syield();
    group.peer_done=1;
    Pterm(44);
}
static void group_wait_child(void *arg)
{
    (void)arg; group.ready|=1;
    while (!group.child_go) (void)Syield();
    group.child_done=1;
    Pterm(45);
}
static void group_compete(void *arg)
{
    (void)arg; group.ready|=2;
    while (!group.requested) (void)Syield();
    (void)term_group(99); /* accepted first status must remain 61 */
    group.returned=1;
}
static void group_leader(void *arg)
{
    int32_t pid;
#if defined(TEST_WITH_PTHJOIN)
    int32_t tid;
#endif
    (void)arg;
    if (launch_entry(group.mode==3 ? group_wait_child : group_target,
                     group.mode==3 ? CLONE_VM : CLONE_VM|CLONE_SAME_PROCESS,
                     group.stack[1],&group.wait_child,&group.target)<=0 ||
        launch_entry(group.mode==3 ? group_compete : group_joiner,
                     CLONE_VM|CLONE_SAME_PROCESS,group.stack[2],&pid,&group.joiner)<=0 ||
        launch_entry(group_killer,CLONE_VM|CLONE_SAME_PROCESS,group.stack[3],&pid,&group.killer)<=0) {
        group.error=1; (void)term_group(90); return;
    }
    if (group.mode==2) Pterm(37); /* anchor must outlive the initial THREAD */
    if (!group.mode) {
        while ((group.ready&3)!=3) (void)Syield();
        group.requested=1;
        (void)term_group(60);
    } else {
        if (group.mode==3) (void)Pwaitpid(group.wait_child,0,0);
#if defined(TEST_WITH_PTHJOIN)
        else (void)join_tid(group.killer,&tid); /* cancellation-safe blocked join */
#else
        else for (;;) (void)Syield();
#endif
    }
    group.returned=1;
}
static void test_group_exit(void)
{
    int i, mode, spin;
    int32_t pid,tid,peer_pid,peer_tid;
    long result;
    const char *files[]={"GDEEP.ON","GGO.ON","GRET0.ON","GRET1.ON","GRET2.ON"};
    for (i=0;i<6;i++) {
        group.stack[i]=(void *)Malloc(RUNNABLE_STACK_SIZE);
        ptest_assert_msg((long)group.stack[i]>0,"group stack allocated");
        if ((long)group.stack[i]<=0) return;
    }
    for (mode=0;mode<
#if defined(__x86_64__) && defined(TEST_WITH_CUIS)
        14
#else
        5
#endif
        ;mode++) {
        for (i=0;i<5;i++) (void)Fdelete(files[i]);
        group.session=group.window=-1; group.wait_done=0;
        group.mode=mode; group.ready=group.requested=group.error=group.returned=group.peer_done=group.peer_go=group.child_go=group.child_done=group.atexit_calls=0;
        ptest_assert_msg(launch_entry(group_peer,CLONE_VM,group.stack[5],&peer_pid,&peer_tid)>0,"independent shared-ASPACE peer created");
        ptest_assert_msg(launch_entry(group_leader,CLONE_VM|CLONE_COPY_STANDARD_HANDLES|CLONE_COPY_CWD,group.stack[0],&pid,&tid)>0,"group PROCESS created");
        for (spin=0;spin<2000 && !group.requested;spin++) (void)Syield();
        ptest_assert_msg(group.requested && !group.error,"group request reached native syscall");
        if (mode==1 || mode==2) {
            ptest_assert_msg(Pwaitpid(pid,1,0)==0,"nested child retains pending PROCESS until safe return");
            ptest_assert_msg(group_exists("GDEEP.ON") && !group_exists("GRET0.ON"),"nested child not unwound or killed");
            for (spin=0;spin<32;spin++) {
                (void)Syield();
                ptest_assert_msg(Pwaitpid(pid,1,0)==0,"nonreturning child delays completion without early reap");
            }
            /* Heap lease barriers return ERR; EACCDN is the separate callback-pin path. */
            ptest_assert_msg(Mfree(group.stack[1])==-1L,"pending nested continuation retains its user stack");
            group_mark("GGO.ON");
            ptest_assert_msg(group_exists("GGO.ON"),"nested release marker exists");
        }
        result=Pwaitpid(pid,0,0);
        ptest_assert_msg((result>>16)==pid && (result&65535)==(mode==4?62:mode?61:60),"group final status and one PROCESS completion");
        ptest_assert_msg(!group.returned && !group.error,"cancelled parent continuations never return to user");
        ptest_assert_msg(Pwaitpid(pid,1,0)==-33L,"group zombie consumed only once");
        if (mode==1 || mode==2) ptest_assert_msg(group_exists("GRET0.ON") && group_exists("GRET1.ON") && group_exists("GRET2.ON"),"all nested PROCESSes complete normally before cancellation");
        if (mode==12) ptest_assert_msg(!group.wait_done,"admitted combined input wait is cancelled before timer commit");
        if (mode==4) ptest_assert_msg(group.atexit_calls==1,"libc exit runs atexit once before group termination");
        group.child_go=1;
        if (mode==3) {
            for (spin=0;spin<128 && !group.child_done;spin++) (void)Syield();
            (void)Syield(); (void)Syield();
            ptest_assert_msg(group.child_done,"cancelled PROCESS wait preserves and orphans separate child");
        }
        group.peer_go=1;
        result=Pwaitpid(peer_pid,0,0);
        ptest_assert_msg((result>>16)==peer_pid && (result&65535)==44 && group.peer_done,"separate PROCESS sharing ASPACE survives group exit");
    }
    for (i=0;i<6;i++) ptest_assert_msg(Mfree(group.stack[i])==0,"group root reclaim releases each stack hold");
    for (i=0;i<5;i++) (void)Fdelete(files[i]);
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
#if defined(TEST_WITH_PTERMGRP)
    test_group_exit();
#if defined(__x86_64__) && defined(TEST_WITH_CUIS) && defined(TEST_WITH_PTHJOIN)
    test_native_streams();
    test_native_resume();
#endif
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
#if defined(__mc68000__)
        if (has_fpu && clone_fpu_expected_iar == 0x12345678UL)
            conws("native m68k nonzero FPIAR: PASS\r\n");
#endif
        conws("pclone native runnable NEW_PROCESS/SAME_PROCESS: PASS\r\n");
#if defined(TEST_WITH_PTHJOIN)
        conws("native Pthjoin: PASS\r\n");
#endif
#if defined(TEST_WITH_PTERMGRP)
        conws("native Ptermgrp: PASS\r\n");
        conws("native libc exit: PASS\r\n");
#if defined(__x86_64__) && defined(TEST_WITH_CUIS)
        conws("native CUIS delay cancellation: PASS\r\n");
        conws("native CUIS retained event cancellation: PASS\r\n");
        conws("native CUIS window/session retirement: PASS\r\n");
        conws("native CUIS exclusive combined wait lifetime: PASS\r\n");
        conws("native CUIS admitted key/button cancellation: PASS\r\n");
#endif
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
        /* Publish the parent prefix before the fresh worker appends its log. */
        ptest_log_flush();
        child = Pexec(100,"runtests.tos",tail,0);
        ptest_assert_msg(child > 0,"managed clone test worker must start");
        if (child > 0) {
            status = Pwaitpid(child,0,0);
            ptest_assert_msg((status >> 16) == child && !(status & 65535),
                             "managed clone execution worker must pass");
#if defined(__x86_64__) && defined(TEST_WITH_CUIS) && defined(TEST_WITH_PTERMGRP) && defined(TEST_WITH_PTHJOIN)
            if ((status>>16)==child && !(status&65535))
                conws("native CUIS retained stream ownership: PASS (parent verified)\r\n");
#endif
        }
    }
#endif
    ptest_pass();
}
