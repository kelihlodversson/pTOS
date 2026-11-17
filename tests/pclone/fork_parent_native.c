/* Real user-trap COW parent-policy controls, kept small so the classic
 * two-MiB procmem window can still run the full self-Pexec ABI fixture. */
#include "test.h"
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/basepage.h>
#include "trap.h"

static volatile struct {
    unsigned long cell;
    unsigned char *heap;
} private_probe;
static long private_query(unsigned slot) { return pclone_trap(0,0xc8920100UL+slot); }
#if defined(__x86_64__)
/* Real syscall at a deliberately selected source-stack boundary. R8 carries
 * this C continuation's original SP in both private return images. */
static long stack_edge_trap(unsigned long stack)
{
    register unsigned long long result __asm__("rax")=(1ULL<<32)|GEMDOS_PCLONE;
    register unsigned long long argument __asm__("rdx")=stack;
    __asm__ volatile("mov %%rsp,%%r8\n\tmov %%rdx,%%rsp\n\tsyscall\n\tmov %%r8,%%rsp"
        : "+a"(result), "+d"(argument) : "D"(0ULL), "S"(0x895fc000ULL)
        : "rcx", "r11", "r8", "memory", "cc");
    return (long)(int)result;
}
#elif defined(__arm__)
static long stack_edge_trap(unsigned long stack)
{
    register long result __asm__("r0")=GEMDOS_PCLONE;
    register long address __asm__("r1")=0;
    register unsigned long selector __asm__("r2")=0x895fc000UL;
    register unsigned long pointer __asm__("r3")=stack;
    __asm__ volatile("mov r8,sp\n\tmov sp,r3\n\tsvc 1\n\tmov sp,r8"
        : "+r"(result), "+r"(pointer) : "r"(address), "r"(selector)
        : "r8", "r12", "lr", "memory", "cc");
    return result;
}
#endif
/* Actual original C continuation and private stack, no fresh user entry.
 * The parent blocks through this exact child's final completion. */
static void cow_vfork_worker(void *argument)
{
    int bad;
    (void)argument;
    /* Leader has stopped before this fresh THREAD's first dispatch. The
     * exact child's PROCESS still lives; original parent must remain blocked. */
    bad=pclone_trap(0,0x895ffffdUL)!=1;
    (void)Syield();
    if(pclone_trap(0,0x895ffffdUL)!=1)bad=1;
    Pterm(bad?74:73);
}
static void test_cow_vfork_continuation(void)
{
    volatile unsigned long local=0x13572468UL;
    long pid,status,parent=Pgetpid(),tid=private_query(3),root=private_query(0);
    long dependencies=pclone_trap(0,0x895ffffcUL),rollback;
    int cycle,stage;
#if defined(__x86_64__) || defined(__arm__)
    ptest_assert_msg(stack_edge_trap(0)==-32L && pclone_trap(0,0x895ffffcUL)==dependencies,
                     "saved-return stack-window subtraction rejects underflow before resources");
    {
        /* TOP-8, and SP at the exclusive stack top itself: neither SYSCALL
         * nor SVC needs user-stack bytes above SP. */
        static const unsigned long edge[2]={0x7ffffff8UL,0x80000000UL};
        int i;
        for(i=0;i<2;i++) {
            pid=stack_edge_trap(edge[i]);
            if(pid==0)Pterm(73);
            ptest_assert_msg(pid>0,i?"saved-return accepts SP at the exclusive stack top":
                             "saved-return accepts valid shallow top-of-stack continuation");
            if(pid>0) {
                status=Pwaitpid(pid,1,0);
                ptest_assert_msg((status>>16)==pid && (status&65535)==73 && Pwaitpid(pid,1,0)<0,
                                 "top-of-stack child resumes after trap and completes before parent return");
            }
        }
    }
#endif
    ptest_assert_msg(pclone_trap(0,0x895f4000UL)==-32L &&
                     pclone_trap(0,0x895ffffcUL)==dependencies,
                     "suspended-parent selector rejects SHARE_VM policy before resources");
    for(stage=1;stage<=8;stage++) {
        rollback=pclone_trap(0,0x895fc000UL+stage);
        if(rollback!=-39L) {
            char message[96];
            sprintf(message,"COW parent preparation stage=%d error=%ld\r\n",stage,rollback);
            conws(message);
        }
        ptest_assert_msg(rollback==-39L,
                         "COW suspended-parent preparation rollback publishes no child");
        ptest_assert_msg(pclone_trap(0,0x895ffffcUL)==dependencies,
                         "failed COW suspended-parent preparation releases exact dependency");
    }
    ptest_assert_msg(pclone_trap(0,0xc8920600UL)==0,"arm final publication cancellation for COW parent wait");
    ptest_assert_msg(pclone_trap(0,0x895fc000UL)==-326L &&
                     pclone_trap(0,0x895ffffcUL)==dependencies,
                     "cancelled final publication releases dependency and leaves parent runnable");
    for(cycle=0;cycle<8;cycle++) {
        private_probe.cell=0x11223344UL;private_probe.heap[17]=0x31;
        local=0x13572468UL;
        pid=pclone_trap(0,0x895fc000UL);
        if(pid==0) {
            int bad=Pgetpid()==parent || Pgetppid()!=parent || private_query(0)==root || private_query(3)==tid;
            if(local!=0x13572468UL || private_probe.cell!=0x11223344UL || private_probe.heap[17]!=0x31)bad=1;
            if(pclone_trap(0,0x895ffffdUL)!=1)bad=1;
            local=0x24681357UL;private_probe.cell=0x66778899UL;private_probe.heap[17]=0x71;
            /* A failed exec never releases the parent: public Pexec(200)
             * admits this private-VM child and fails to find the file. */
#if defined(TEST_WITH_PEXEC200)
            if(Pexec(200,"MISSING.TOS","",0)!=-33L || pclone_trap(0,0x895ffffdUL)!=1)bad=1;
#else
            if(Pexec(200,"MISSING.TOS","",0)!=-32L || pclone_trap(0,0x895ffffdUL)!=1)bad=1;
#endif
            if(cycle==0 || cycle==1) {
                long nested=pclone_trap(0,cycle?0x895fc000UL:0x895f8000UL);
                if(nested==0) { local=0;private_probe.heap[17]=0xff;Pterm(75); }
                if(nested<=0)bad=1;
                else {
                    long reaped=Pwaitpid(nested,0,0);
                    if((reaped>>16)!=nested || (reaped&65535)!=75 || local!=0x24681357UL ||
                       private_probe.heap[17]!=0x71 || pclone_trap(0,0x895ffffdUL)!=1)bad=1;
                }
            }
            if(!bad && cycle==2) {
                struct ptos_clone_args args={0};
                args.flags=CLONE_VM|CLONE_SAME_PROCESS;args.entry=cow_vfork_worker;
                args.stack_base=(void *)Malloc(32768);args.stack_size=32768;
                if(!args.stack_base || pclone_trap(&args,sizeof(args))<=0)bad=1;
            }
#if defined(TEST_WITH_PTERMGRP)
            if(!bad && cycle==3) (void)trap_1_ww(GEMDOS_PTERMGRP,79);
#endif
            if(!bad && cycle==4) *(volatile long *)0=1;
            Pterm(bad?74:73);
            for(;;) { }
        }
        ptest_assert_msg(pid>0 && pid!=parent,"COW suspended parent resumes with child PID");
        if(pid>0) {
            /* WNOHANG must already deliver final completion: parent could
             * not run user code while this exact child still used its image. */
            status=Pwaitpid(pid,1,0);
            ptest_assert_msg((status>>16)==pid && (status&65535)==(
#if defined(TEST_WITH_PTERMGRP)
                             cycle==3?79:
#endif
                             cycle==4?0x0b00:73),
                             "COW parent remains blocked through failed exec, unrelated child and leader-only exit");
            ptest_assert_msg(Pwaitpid(pid,1,0)<0,"COW parent dependency does not consume public reap");
            ptest_assert_msg(local==0x13572468UL && private_probe.cell==0x11223344UL && private_probe.heap[17]==0x31,
                             "COW Pvfork child private writes do not change parent stack/global/heap");
            ptest_assert_msg(pclone_trap(0,0x895ffffcUL)==dependencies,
                             "COW parent dependency releases exactly once before continuation");
        }
    }
#if defined(TEST_WITH_PTERMGRP)
    {
        long survived=pclone_trap(0,0x895ffff9UL);
        long before[7],after[7];
        static const unsigned query[]={0x80,0x81,0x82,0x83,0x85,0x86,0x87};
        unsigned q;
        for(q=0;q<7;q++)before[q]=pclone_trap(0,0xc8920700UL+query[q]);
        pid=pclone_trap(0,0x895f8000UL);
        if(pid==0) {
            long child=pclone_trap(0,0x895fc000UL);
            if(child==0) {
                /* Let the existing native admission wait/resume probe run
                 * while its parent-BLOCKED precondition holds. Cancellation
                 * deliberately ends that precondition in the next step. */
                if(pclone_trap(0,0x895ffffdUL)!=1)Pterm(74);
                (void)Syield();
                if(pclone_trap(0,0x895ffffdUL)!=1)Pterm(74);
                if(pclone_trap(0,0x895ffffbUL)!=0)Pterm(74);
                (void)Syield();(void)Syield();
                if(Pgetppid()!=0 || pclone_trap(0,0x895ffffaUL)!=0)Pterm(74);
                Pterm(73);
            }
            /* Exact parent cancellation must terminate before user resume. */
            Pterm(74);
        }
        ptest_assert_msg(pid>0,"COW cancellation fixture publishes independently");
        if(pid>0) {
            status=Pwaitpid(pid,0,0);
            ptest_assert_msg((status>>16)==pid && (status&65535)==82,
                             "COW suspended parent cancellation delivered after dependency cleanup");
            (void)Syield();(void)Syield();
            ptest_assert_msg(pclone_trap(0,0x895ffff9UL)==survived+1 &&
                             pclone_trap(0,0x895ffffcUL)==dependencies,
                             "COW child survives parent cancellation in its own VM and exits independently");
            for(q=0;q<7;q++) {
                after[q]=pclone_trap(0,0xc8920700UL+query[q]);
                ptest_assert_msg(before[q]==after[q],"COW cancellation restores exact PROCESS/THREAD/root/stack identity references");
            }
        }
    }
#endif
}

#if defined(TEST_WITH_PEXEC200)
/* A Pvfork child that replaces itself with Pexec(200) releases its parent
 * at the commit, while it still runs its new image (this program, tail "W",
 * which waits for C:\FORKGO.TMP). The child has no inherited directory. */
static void test_cow_vfork_exec_release(void)
{
    long dependencies=pclone_trap(0,0x895ffffcUL),pid,status,go;
    (void)Fdelete("C:\\FORKGO.TMP");
    pid=pclone_trap(0,0x895fc000UL);
    if(pid==0) {
        if(pclone_trap(0,0x895ffffdUL)!=1)Pterm(74); /* parent still blocked */
        (void)Pexec(200,"C:\\FORKPAR.TOS","\001W",0);
        Pterm(75); /* the exec failed */
        for(;;) { }
    }
    ptest_assert_msg(pid>0,"COW Pvfork child for a committed exec");
    if(pid<=0) return;
    ptest_assert_msg(Pwaitpid(pid,1,0)==0,
                     "COW Pvfork parent released at the child's Pexec(200) commit, before its completion");
    ptest_assert_msg(pclone_trap(0,0x895ffffcUL)==dependencies,
                     "COW parent dependency released exactly once at the exec commit");
    go=Fcreate("C:\\FORKGO.TMP",0);
    ptest_assert_msg(go>=0,"release the replaced Pvfork child");
    if(go>=0) (void)Fclose((short)go);
    status=Pwaitpid(pid,0,0);
    ptest_assert_msg((status>>16)==pid && (status&65535)==76,
                     "replaced Pvfork child completes once, from its new image");
    ptest_assert_msg(Pwaitpid(pid,1,0)<0,"replaced Pvfork child reaped once");
    ptest_assert_msg(Fdelete("C:\\FORKGO.TMP")==0,"Pvfork exec release test file");
}
#endif
#if defined(TEST_WITH_PFORK) && !defined(__mc68000__)
/* Pfork with untouched, demand-paged stack pages just below its SP: an
 * alloca()'d gap is never written, and its size sweeps SP across a page. */
static long __attribute__((noinline)) fork_below_gap(void)
{
    long pid=Pfork();
    if(pid==0) Pterm(73);
    return pid;
}
static int __attribute__((noinline)) fork_at_depth(unsigned long depth)
{
    char *gap=(char *)__builtin_alloca(depth);
    long pid,status;
    __asm__ volatile("" : : "r"(gap) : "memory");
    pid=fork_below_gap();
    if(pid<=0) return 0;
    status=Pwaitpid(pid,0,0);
    return (status>>16)==pid && (status&65535)==73 && Pwaitpid(pid,1,0)<0;
}
#endif
#if defined(TEST_WITH_PFORK)
/* Public MiNT Pfork()/Pvfork() (CONF_WITH_PFORK). The child inherits every
 * descriptor with its shared file position, the current directory, the DTA
 * address and its parent; its private writes stay its own. */
static void test_public_fork(void)
{
    long fd,pid,status,go,parent=Pgetpid();
    char buffer[4];
    volatile unsigned long local=0x5a5a1234UL;
    void *dta=(void *)Fgetdta();
    (void)Fdelete("FORKPUB.TMP");
    fd=Fcreate("FORKPUB.TMP",0);
    ptest_assert_msg(fd>=0,"public fork descriptor in the current directory");
    if(fd<0) return;
    ptest_assert_msg(Fwrite((short)fd,1,"A")==1,"parent writes before Pfork");
    /* An open directory search on the inherited DTA (#984). */
    ptest_assert_msg(Fsfirst("*.TOS",0)==0,"parent starts a directory search before Pfork");
    pid=Pfork();
    if(pid==0) {
        int bad=Pgetppid()!=parent || Pgetpid()==parent || (void *)Fgetdta()!=dta ||
                local!=0x5a5a1234UL;
        local=0;
        if(Fwrite((short)fd,1,"B")!=1 || Fclose((short)fd)) bad=1;
        if(Fsnext()!=0) bad=1; /* continues the inherited search */
        Pterm(bad?74:73);
        for(;;) { }
    }
    ptest_assert_msg(pid>0 && pid!=parent,"Pfork returns the child's PID to its parent");
    if(pid>0) {
        status=Pwaitpid(pid,0,0);
        ptest_assert_msg((status>>16)==pid && (status&65535)==73,
                         "Pfork child inherits the descriptor, directory, DTA, search and parent");
        ptest_assert_msg(Pwaitpid(pid,1,0)<0,"Pfork child reaped once");
        ptest_assert_msg(local==0x5a5a1234UL,"Pfork child's private writes do not reach the parent");
        ptest_assert_msg(Fsnext()==0,"parent's own search continues independently of the child's");
    }
    ptest_assert_msg(Fwrite((short)fd,1,"C")==1,"parent descriptor survives the child's close");
    ptest_assert_msg(Fclose((short)fd)==0,"parent closes its own binding");
    fd=Fopen("FORKPUB.TMP",0);
    memset(buffer,0,sizeof(buffer));
    ptest_assert_msg(fd>=0 && Fread((short)fd,3,buffer)==3 && !memcmp(buffer,"ABC",3),
                     "parent and Pfork child share one file position");
    if(fd>=0) (void)Fclose((short)fd);
    ptest_assert_msg(Fdelete("FORKPUB.TMP")==0,"public fork test file");
    /* Pvfork: the parent resumes at the child's committed Pexec(200), here
     * by a path relative to the inherited directory. */
    (void)Fdelete("C:\\FORKGO.TMP");
    pid=Pvfork();
    if(pid==0) {
        /* The parent must be blocked on this exact child before its exec:
         * a Pvfork that behaved like Pfork would fail here. */
        if(pclone_trap(0,0x895ffffdUL)!=1) Pterm(77);
        (void)Pexec(200,"FORKPAR.TOS","\001W",0);
        Pterm(75);
        for(;;) { }
    }
    ptest_assert_msg(pid>0 && pid!=parent,"Pvfork returns the child's PID to its parent");
    if(pid<=0) return;
    ptest_assert_msg(Pwaitpid(pid,1,0)==0,"Pvfork parent resumes at the child's Pexec(200) commit");
    go=Fcreate("C:\\FORKGO.TMP",0);
    if(go>=0) (void)Fclose((short)go);
    status=Pwaitpid(pid,0,0);
    ptest_assert_msg((status>>16)==pid && (status&65535)==76,"Pvfork child completes once from its new image");
    ptest_assert_msg(Pwaitpid(pid,1,0)<0,"Pvfork child reaped once");
    ptest_assert_msg(Fdelete("C:\\FORKGO.TMP")==0,"Pvfork test file");
#if !defined(__mc68000__)
    {
        unsigned long offset;
        int forked=1;
        for(offset=0;offset<4096;offset+=256)
            if(!fork_at_depth(0x4000UL+offset)) forked=0; /* inside an inherited small stack limit */
        ptest_assert_msg(forked,"Pfork with untouched demand-paged stack pages below SP");
    }
#endif
}
#endif
int main(void)
{
    int failures;
#if defined(TEST_WITH_PEXEC200)
    if((unsigned char)_base->p_cmdlin[0]>=1 && _base->p_cmdlin[1]=='W') {
        /* The image a Pvfork child replaced itself with. */
        while(Fsfirst("C:\\FORKGO.TMP",0)) (void)Syield();
        Pterm(76);
    }
#endif
    ptest_log_append();
    ptest_begin("COW suspended parent dependency (test-only)");
    private_probe.heap=(unsigned char *)Malloc(12288);
    ptest_assert_msg(private_probe.heap!=NULL,"COW parent-policy private heap reservation");
    if(private_probe.heap) {
        test_cow_vfork_continuation();
#if defined(TEST_WITH_PEXEC200)
        test_cow_vfork_exec_release();
#endif
#if defined(TEST_WITH_PFORK)
        test_public_fork();
#endif
        ptest_assert_msg(Mfree(private_probe.heap)==0,"COW parent-policy heap released after final child reap");
    }
    ptest_pass();
    failures=ptest_failures();
    if(!failures)conws("native COW Pvfork parent dependency (test-only, no exec commit): PASS\r\n");
#if defined(TEST_WITH_PEXEC200)
    if(!failures)conws("native COW Pvfork parent released at committed Pexec(200): PASS\r\n");
#endif
#if defined(TEST_WITH_PFORK)
    if(!failures)conws("native public Pfork/Pvfork with inherited descriptors: PASS\r\n");
#endif
    ptest_log_flush();
    Pterm(failures);
    return failures;
}
