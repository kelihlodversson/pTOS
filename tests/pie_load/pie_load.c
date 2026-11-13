/*
 * pie_load.c - regression test for the ELF loader's PIE support
 *
 * Launches pie_probe.c, a separate executable built and linked as a
 * position-independent ET_DYN ("ld -pie --no-dynamic-linker"), via
 * Pexec() and checks its exit code. pie_probe.c dereferences a global
 * pointer that only resolves correctly if elf_pgmld() applied the
 * dynamic (.rel.dyn / .rela.dyn) relocations a PIE carries -- exactly
 * the code path a prior regression silently skipped whenever a
 * relocation's sh_info was 0 (see bdos/elfld.c).
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(TEST_WITH_SHARED_VM_PEXEC)
#include "shared_vm_probe.h"
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#include "../../include/mman.h"
static void shared_vm_diagnostic(const char *label,long value)
{
    char number[11];unsigned i;
    for(i=0;i<8;i++) number[i]="0123456789abcdef"[((unsigned long)value>>(28-4*i))&15];
    number[8]='\r';number[9]='\n';number[10]=0;conws(label);conws(number);
}
#if defined(__mc68000__)
static void shared_vm_prg(void)
{
    /* Real absolute PRG: text ends at the first page boundary after the
     * basepage; data and BSS occupy their own non-executable pages. */
    static const unsigned char header[28]={
        0x60,0x1a, 0,0,0x0f,0, 0,0,0x10,0, 0,0,0x10,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,1};
    static const unsigned char code[]={
        0x20,0x6f,0,4,                         /* movea.l 4(sp),a0: basepage */
        0x21,0x7c,0x6a,0x5b,0x4c,0x3d,0x10,0, /* move.l #marker,4096(a0) */
        0x3f,0x3c,0x12,0x34,                   /* Pterm(0x1234) */
        0x3f,0x3c,0,0x4c,0x4e,0x41,0x4a,0xfc};
    static unsigned char contents[0x1f00];
    long fd,rc;unsigned i;
    memset(contents,0,sizeof(contents));memcpy(contents,code,sizeof(code));
    fd=Fcreate("C:\\SHARED.PRG",0);
    ptest_assert_msg(fd>=0,"shared PRG: create native loader fixture");
    if(fd<0) return;
    ptest_assert_msg(Fwrite(fd,sizeof(header),header)==sizeof(header) &&
        Fwrite(fd,sizeof(contents),contents)==sizeof(contents),"shared PRG: complete text/data fixture");
    ptest_assert_msg(Fclose(fd)==0,"shared PRG: fixture close");
    for(i=0;i<2;i++) {
        BASEPAGE *bp=(BASEPAGE *)Pexec(PE_LOAD,"C:\\SHARED.PRG","",0);
        char *env;volatile unsigned long *data;
        ptest_assert_msg((long)bp>0,"shared PRG: real PE_LOAD");if((long)bp<=0) continue;
        env=bp->p_env;data=(volatile unsigned long *)bp->p_dbase;
        ptest_assert_msg(*data==0,"shared PRG: staged data initially zero");
        rc=Pexec(i?104:4,0,bp,0);
        ptest_assert_msg(i?rc>0:rc==0x1234,"shared PRG: native entry and full synchronous status");
        if(i && rc>0) {
            long waited=Pwaitpid(rc,0,0);
            ptest_assert_msg((waited>>16)==rc && (waited&65535)==0x34,"shared PRG: canonical PID/status reap");
            ptest_assert_msg(Pwaitpid(rc,1,0)==-33,"shared PRG: one public reap");
        }
        ptest_assert_msg(*data==0x6a5b4c3dUL,"shared PRG: child writes shared source data");
        ptest_assert_msg(Mfree(bp)==0 && Mfree(env)==0,"shared PRG: final raw allocation release");
    }
    ptest_assert_msg(Fdelete("C:\\SHARED.PRG")==0,"shared PRG: fixture unlink");
    conws("native shared-VM PRG Pexec4/104 entry, status and shared data: tested\r\n");
}
#endif
#if defined(TEST_WITH_ELF_SHARED)
static void shared_vm_interpreted_result(unsigned mode,long result)
{
    char text[]="shared interpreter result mode=0 value=00000000\r\n";
    static const char hex[]="0123456789abcdef";
    unsigned i;unsigned long value=(unsigned long)result;
    text[31]=(char)('0'+mode);
    for(i=0;i<8;i++) text[39+i]=hex[(value>>(28-4*i))&15];
    conws(text);
}
static void shared_vm_interpreted_peers(void)
{
    unsigned completed_first;
    for(completed_first=0;completed_first<2;completed_first++) {
        BASEPAGE *images[2]={0,0};char *envs[2]={0,0};long pids[2]={0,0};
        unsigned i;
        for(i=0;i<2;i++) {
            images[i]=(BASEPAGE *)Pexec(PE_LOAD,"C:\\SHRINT.ELF","", "CHECK=phase5\0\0");
            ptest_assert_msg((long)images[i]>0,"shared interpreter peers: PE_LOAD");
            if((long)images[i]<=0) {images[i]=0;break;}
            envs[i]=images[i]->p_env;pids[i]=Pexec(104,0,images[i],0);
            ptest_assert_msg(pids[i]>0,"shared interpreter peers: retain first image while admitting second");
            if(i==0 && !completed_first && pids[0]>0)
                ptest_assert_msg(Pwaitpid(pids[0],1,0)==0,"shared interpreter peers: first child still live before second admission");
            if(i==0 && completed_first && pids[0]>0) {
                long waited=Pwaitpid(pids[0],0,0);
                ptest_assert_msg((waited>>16)==pids[0] && (waited&65535)==77,
                    "shared interpreter peers: completed first image remains staged-owned");
                ptest_assert_msg(Pwaitpid(pids[0],1,0)==-33,"shared interpreter peers: first child one reap");
                pids[0]=0;
            }
        }
        for(i=0;i<2;i++) if(pids[i]>0) {
            long waited=Pwaitpid(pids[i],0,0);
            ptest_assert_msg((waited>>16)==pids[i] && (waited&65535)==77,
                "shared interpreter peers: both PROCESSes complete independently");
            ptest_assert_msg(Pwaitpid(pids[i],1,0)==-33,"shared interpreter peers: one reap per child");
        }
        for(i=0;i<2;i++) if(images[i]) {
            ptest_assert_msg(Mfree(images[i])==0,"shared interpreter peers: image final release");
            ptest_assert_msg(Mfree(envs[i])==0,"shared interpreter peers: environment final release");
        }
    }
    conws("native shared-VM simultaneous and completed-image peers: tested\r\n");
}
static void shared_vm_interpreted(void)
{
    static const char envspec[]="CHECK=phase5\0\0";
    unsigned mode;
    for(mode=0;mode<2;mode++) {
        BASEPAGE *bp=(BASEPAGE *)Pexec(PE_LOAD,"C:\\SHRINT.ELF","",envspec);
        char *env;long rc;
        if((long)bp<=0) shared_vm_interpreted_result(2+mode,(long)bp);
        ptest_assert_msg((long)bp>0,"shared interpreter: PE_LOAD");if((long)bp<=0) continue;
        env=bp->p_env;rc=Pexec(mode?104:4,0,bp,0);
        shared_vm_interpreted_result(mode,rc);
        if(rc<0) {conws("shared interpreter launch error\r\n");}
        ptest_assert_msg(mode?rc>0:rc==77,"shared interpreter: auxv, relocation, RELRO and fini");
        if(mode && rc>0) {
            long waited=Pwaitpid(rc,0,0);
            shared_vm_interpreted_result(mode,waited);
            ptest_assert_msg((waited>>16)==rc && (waited&65535)==77,"shared interpreter: canonical PROCESS wait");
            ptest_assert_msg(Pwaitpid(rc,1,0)==-33,"shared interpreter: exactly one reap");
        }
        ptest_assert_msg(Mfree(bp)==0,"shared interpreter: final image cleanup");
        ptest_assert_msg(Mfree(env)==0,"shared interpreter: final environment cleanup");
    }
    conws("native shared-VM PT_INTERP Pexec4/104 standard entry: tested\r\n");
}
#endif
static void shared_vm_status(void)
{
    unsigned mode,kind;
    for(mode=0;mode<2;mode++) for(kind=0;kind<2;kind++) {
        char tail[3]={1,0,0};
        BASEPAGE *bp;char *env;long rc,expected;
        tail[1]=kind?'i':'f';
        bp=(BASEPAGE *)Pexec(PE_LOAD,"C:\\PIEPROBE.TOS",tail,0);
        ptest_assert_msg((long)bp>0,"shared status: PE_LOAD");if((long)bp<=0) continue;
        env=bp->p_env;rc=Pexec(mode?104:4,0,bp,0);
        if(mode) {
            ptest_assert_msg(rc>0,"shared104 status: PID");
            if(rc>0) {
                long waited=Pwaitpid(rc,0,0);
                expected=kind?0x0400:0x00ff;
                ptest_assert_msg((waited>>16)==rc && (waited&65535)==expected,
                    "shared104: normal ffff and SIGILL use canonical distinct encoding");
                ptest_assert_msg(Pwaitpid(rc,1,0)==-33,"shared status: one reap");
            }
        } else ptest_assert_msg(rc==(kind?0x0400:0xffff),
                    "shared4: full normal ffff distinct from fault SIGILL");
        ptest_assert_msg(Mfree(bp)==0,"shared status: reclaim image after normal/fault");
        ptest_assert_msg(Mfree(env)==0,"shared status: reclaim environment after normal/fault");
    }
    conws("native shared-VM full-status/fault and canonical wait: tested\r\n");
}
static void shared_vm_tail(char *tail,char role,struct shared_vm_probe *probe)
{
    unsigned i;uintptr_t address=(uintptr_t)probe;
    tail[0]=10;tail[1]=role;tail[2]=' ';
    for(i=0;i<8;i++) tail[3+i]="0123456789abcdef"[(address>>(28-4*i))&15];
    tail[11]=0;
}
static void shared_vm_limits(void)
{
    static struct shared_vm_probe probe;
    long core=Psetlimit(2,-1),data=Psetlimit(3,-1),stack=Psetlimit(0x100,-1),result;
    BASEPAGE *image;void *env;char tail[12];
    memset(&probe,0,sizeof(probe));
    ptest_assert_msg(Psetlimit(2,96L*1024*1024)==core &&
        Psetlimit(3,64L*1024*1024)==data && Psetlimit(0x100,1024L*1024)==stack,
        "shared limits: prepare source limits");
    shared_vm_tail(tail,'p',&probe);
    image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
    if((long)image>0) {
        env=image->p_env;result=Pexec(4,0,image,0);
        shared_vm_diagnostic("shared limit CORE=",probe.core_limit);
        shared_vm_diagnostic("shared limit DATA=",probe.data_limit);
        shared_vm_diagnostic("shared limit STACK=",probe.stack_limit);
        shared_vm_diagnostic("shared limit terminal status=",result);
        shared_vm_diagnostic("shared limit phantom write completed=",probe.done);
        ptest_assert_msg(probe.started && probe.core_limit==96UL*1024*1024 &&
            probe.data_limit==64UL*1024*1024 && probe.stack_limit==1024UL*1024,
            "shared limits: child inherits source CORE/DATA/stack policy");
        ptest_assert_msg(probe.same_limit==1024UL*1024 && (int32_t)probe.resize_limit==-36,
            "shared limits: unchanged cap accepted and stack relocation refused");
        ptest_assert_msg(result==0x0b00 && !probe.done,
            "shared limits: no synthetic default-stack materialization authority");
        ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared limits: reclaim faulted child image");
    } else ptest_assert_msg(0,"shared limits: stage child");
    (void)Psetlimit(2,core);(void)Psetlimit(3,data);(void)Psetlimit(0x100,stack);
    conws("native shared-VM inherited limits and exact stack authority: tested\r\n");
}

static void shared_vm_orphan(void)
{
    static struct shared_vm_probe probe;
    char tail[12];BASEPAGE *image;char *env;long result;unsigned spin;
    memset(&probe,0,sizeof(probe));shared_vm_tail(tail,'o',&probe);
    image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
    ptest_assert_msg((long)image>0,"shared orphan: source PE_LOAD");if((long)image<=0) return;
    env=image->p_env;result=Pexec(4,0,image,0);
    ptest_assert_msg(result==0x32 && probe.child_pid && !probe.error,
        "shared orphan: source returns after independently publishing child");
    for(spin=0;spin<512 && !probe.started && !probe.error;spin++) (void)Syield();
    ptest_assert_msg(probe.started && !probe.done && !probe.error,"shared orphan: child runs after source PROCESS retirement");
    if(probe.started && !probe.done) {
        ptest_assert_msg(Mvalidate(0,(void *)(uintptr_t)probe.staged,256,0)==0 &&
            Mvalidate(0,(void *)(uintptr_t)probe.environment,1,0)==0,
            "shared orphan: child basepage/environment survive source retirement");
        ptest_assert_msg(Mfree(image)==-36 && Mfree(env)==-36,
            "shared orphan: source compatibility allocations retained while child executes");
    }
    probe.gate=1;
    for(spin=0;spin<512 && !probe.done && !probe.error;spin++) (void)Syield();
    for(spin=0;spin<8;spin++) (void)Syield();
    ptest_assert_msg(probe.done && !probe.error,"shared orphan: child stages a further image and exits independently");
    if(probe.staged) ptest_assert_msg(Mvalidate(0,(void *)(uintptr_t)probe.staged,256,0)==-25 &&
        Mvalidate(0,(void *)(uintptr_t)probe.environment,1,0)==-25,
        "shared orphan: child allocations reclaimed before shared root dies");
    if(probe.grandchild) ptest_assert_msg(Mvalidate(0,(void *)(uintptr_t)probe.grandchild,256,0)==-25 &&
        Mvalidate(0,(void *)(uintptr_t)probe.grand_environment,1,0)==-25,
        "shared orphan: unstarted grandchild allocations reclaimed exactly once");
    ptest_assert_msg(Pwaitpid(probe.child_pid,1,0)==-33,"shared orphan: unrelated PROCESS cannot consume an orphan result");
    ptest_assert_msg(Mfree(image)==0,"shared orphan: source image final release");
    ptest_assert_msg(Mfree(env)==0,"shared orphan: source environment final release");
    conws("native shared-VM running orphan and nested unstarted cleanup: tested\r\n");
}
#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_PTHJOIN)
static void shared_vm_controller(void *argument)
{
    struct shared_vm_probe *probe=argument;unsigned spin;long result;
    for(spin=0;spin<512 && !probe->worker_started;spin++) (void)Syield();
    if(!probe->worker_started) {probe->error=31;probe->gate=1;return;}
    result=Pwaitpid(probe->pid,1,0);probe->controller_result=(uint32_t)result;
    if(result!=-36) probe->error=32; /* another TID owns mode4's completion */
    probe->gate=1;
}
static void shared_vm_last_thread(void)
{
    static struct shared_vm_probe probe;
    unsigned asynchronous;
    for(asynchronous=0;asynchronous<2;asynchronous++) {
        char tail[12];BASEPAGE *image;char *env;void *controller=NULL;
        long result,controller_tid=0;unsigned spin;int32_t status=-1;
        memset(&probe,0,sizeof(probe));shared_vm_tail(tail,'l',&probe);
        image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
        ptest_assert_msg((long)image>0,"shared THREAD lifetime: source PE_LOAD");if((long)image<=0) continue;
        env=image->p_env;
        if(!asynchronous) {
            struct ptos_clone_args args={0};controller=(void *)Malloc(16384);
            ptest_assert_msg((long)controller>0,"shared THREAD lifetime: waiter-controller stack");
            if((long)controller<=0) { (void)Mfree(image);(void)Mfree(env);continue; }
            args.flags=CLONE_VM|CLONE_SAME_PROCESS;args.entry=shared_vm_controller;args.argument=&probe;
            args.stack_base=controller;args.stack_size=16384;
            controller_tid=trap_1_wll(GEMDOS_PCLONE,(long)&args,sizeof(args));
            if(controller_tid<=0) shared_vm_diagnostic("shared controller clone error=",controller_tid);
            ptest_assert_msg(controller_tid>0,"shared THREAD lifetime: independent managed waiter controller");
            if(controller_tid<=0) {(void)Mfree(controller);(void)Mfree(image);(void)Mfree(env);continue;}
        }
        result=Pexec(asynchronous?104:4,0,image,0);
        if(asynchronous && result>0) {
            for(spin=0;spin<512 && !probe.worker_started;spin++) (void)Syield();
            ptest_assert_msg(probe.worker_started && !probe.done && !probe.error,
                "shared THREAD lifetime: leader gone and worker still executing");
            ptest_assert_msg(Pwaitpid(result,1,0)==0,"shared THREAD lifetime: initial THREAD exit is not PROCESS completion");
            ptest_assert_msg(Mfree(image)==-36 && Mfree(env)==-36,
                "shared THREAD lifetime: image/env cannot be freed under surviving worker");
#if defined(TEST_WITH_MMAP)
            ptest_assert_msg(trap_1_wlll(GEMDOS_MPROT,(long)((uintptr_t)image->p_tbase&~4095UL),4096,PROT_READ)==-36,
                "shared THREAD lifetime: retained image protection cannot be revoked");
            {
                long unmap=trap_1_wll(GEMDOS_MUNMAP,(long)((uintptr_t)image->p_tbase&~4095UL),4096);
                shared_vm_diagnostic("shared THREAD retained-image Munmap=",unmap);
                /* Loader images lie outside the public mmap window. That
                 * namespace rejection precedes the retained-range preflight. */
                ptest_assert_msg(unmap==-40,
                    "shared THREAD lifetime: loader image is outside Munmap namespace");
            }
#endif
            probe.gate=1;
            {
                long pid=result;result=Pwaitpid(pid,0,0);
                ptest_assert_msg((result>>16)==pid && (result&65535)==0x42,
                    "shared THREAD lifetime: final worker alone completes async PROCESS");
                ptest_assert_msg(Pwaitpid(pid,1,0)==-33,"shared THREAD lifetime: async PROCESS reaped once");
            }
        } else if(!asynchronous) ptest_assert_msg(result==0x42,
            "shared THREAD lifetime: mode4 waits past leader for final THREAD");
        else ptest_assert_msg(0,"shared THREAD lifetime: async launch succeeds");
        if(controller_tid>0) {
            ptest_assert_msg(trap_1_wll(GEMDOS_PTHJOIN,controller_tid,(long)&status)==0 && !status &&
                (long)(int32_t)probe.controller_result==-36,
                "shared THREAD lifetime: competing PROCESS wait cannot steal mode4 result");
            ptest_assert_msg(Mfree(controller)==0,"shared THREAD lifetime: waiter-controller stack released");
        }
        if(probe.error) shared_vm_diagnostic("shared THREAD lifetime error=",probe.error);
        ptest_assert_msg(!probe.error && probe.done && probe.steps==2 && probe.worker_tid>0 &&
            probe.second_tid>0 && probe.second_tid!=probe.worker_tid,
            "shared THREAD lifetime: surviving worker creates and joins a further THREAD");
        for(spin=0;spin<8;spin++) (void)Syield();
        if(probe.worker_stack) ptest_assert_msg(Mfree((void *)(uintptr_t)probe.worker_stack)==0,
            "shared THREAD lifetime: stopped worker user stack released");
        ptest_assert_msg(Mfree(image)==0,"shared THREAD lifetime: source image final release");
        ptest_assert_msg(Mfree(env)==0,"shared THREAD lifetime: source environment final release");
    }
    conws("native shared-VM final-THREAD completion and competing waiter: tested\r\n");
}
void shared_vm_run_managed_tests(void)
{
    ptest_begin("shared-VM managed THREAD lifetime");
    shared_vm_last_thread();
    ptest_pass();
}
static void shared_vm_managed_worker(void)
{
    static const char tail[]="\13--shared-vm";
    long pid,result;
    /* Synchronous outer Pexec may be a nested unmanaged launcher on ARM and
     * m68k. Admit a real managed PROCESS before exercising public Pclone. */
    ptest_log_flush();pid=Pexec(100,"runtests.tos",tail,0);
    ptest_assert_msg(pid>0,"shared THREAD lifetime: admit managed test PROCESS");
    if(pid>0) {
        result=Pwaitpid(pid,0,0);
        ptest_assert_msg((result>>16)==pid && !(result&65535),
            "shared THREAD lifetime: managed worker passes all assertions");
    }
}
#endif
static void shared_vm_staged_retirement(void)
{
    static struct shared_vm_probe probe;
    char tail[12]={10,'s',' ',0};BASEPAGE *image;char *env;
    unsigned i;uintptr_t address=(uintptr_t)&probe;long result;
    memset(&probe,0,sizeof(probe));
    for(i=0;i<8;i++) tail[3+i]="0123456789abcdef"[(address>>(28-4*i))&15];
    image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
    ptest_assert_msg((long)image>0,"shared staged retirement: source PE_LOAD");
    if((long)image<=0) return;
    env=image->p_env;result=Pexec(4,0,image,0);
    ptest_assert_msg(result==0x31 && probe.done && !probe.error && probe.staged && probe.environment,
        "shared staged retirement: source staged an unstarted child then exited");
    for(i=0;i<8;i++) (void)Syield();
    if(probe.staged) {
        long basepage_check=Mvalidate((int)Pgetpid(),(void *)(uintptr_t)probe.staged,256,0);
        long environment_check=Mvalidate((int)Pgetpid(),(void *)(uintptr_t)probe.environment,1,0);
        shared_vm_diagnostic("shared staged retirement basepage check=",basepage_check);
        shared_vm_diagnostic("shared staged retirement environment check=",environment_check);
        /* Mvalidate uses GEMDOS EINVAL(-25) for inaccessible memory. */
        ptest_assert_msg(basepage_check==-25,
            "shared staged retirement: original construction basepage borrow released");
        ptest_assert_msg(environment_check==-25,
            "shared staged retirement: original construction environment borrow released");
    }
    ptest_assert_msg(Mfree(image)==0,"shared staged retirement: source image final release");
    ptest_assert_msg(Mfree(env)==0,"shared staged retirement: source environment final release");
    conws("native shared-VM unstarted-child source retirement: tested\r\n");
}
static void shared_vm_basic(void)
{
    unsigned i;
    for(i=0;i<2;i++) {
        BASEPAGE *bp=(BASEPAGE *)Pexec(PE_LOAD,"C:\\PIEPROBE.TOS","",0);
        char *env;long result,waited;
        ptest_assert_msg((long)bp>0,"shared VM: PE_LOAD staging succeeds");
        if((long)bp<=0) continue;
        env=bp->p_env;
        result=Pexec(i?104:4,0,bp,0);
        if(result<0) { conws("shared VM launch error: ");
            {unsigned n;for(n=0;n<8;n++) Cconout("0123456789abcdef"[((unsigned long)result>>(28-4*n))&15]);} conws("\r\n"); }
        ptest_assert_msg(i?result>0:result==0,"shared VM: actual 4/104 child executes and exits normally");
        if(i && result>0) {
            waited=Pwaitpid(result,0,0);
            ptest_assert_msg((waited>>16)==result && !(waited&65535),"shared VM104: canonical wait returns child PID/status");
            ptest_assert_msg(Pwaitpid(result,1,0)==-33,"shared VM104: exactly one public reap");
        }
        if(result>=0) ptest_assert_msg(Pexec(4,0,bp,0)==-40,"shared VM: consumed loader target cannot launch twice");
        ptest_assert_msg(Mfree(bp)==0 && Mfree(env)==0,"shared VM: staged owner cleanup after child exit");
    }
    conws("native shared-VM Pexec4/104 execution and canonical reap: tested\r\n");
}
#endif

void test_pie_load(void);

/* the segments follow the basepage back to back only where the image is loaded
 * into the TPA: in an address space of its own it is at the image address */
#if (defined(__arm__) || defined(__mc68000__)) && !defined(TEST_WITH_USER_ASPACE)

/*
 * The sizes in the basepage of a loaded program add up (with the basepage) to
 * the end of its image: Mshrink() to that size must leave the bss.  On ARM
 * the loader moves the image up from the basepage by up to 15 bytes, to keep
 * the alignment of what it holds, by an amount that depends on where the TPA
 * is; the bss length then includes that gap.  So the program is loaded after
 * blocks of 0, 4, 8 ... bytes have moved the TPA, the gap worked out
 * from where the image starts (the data base less the text length), and the
 * length of the bss less the gap must be the same every time.
 */
static const char *check_footprint(void)
{
    static const long pads[] = { 0, 4, 8, 12, 16, 20, 24, 28 };
    void *held[sizeof pads / sizeof pads[0]];
    const char *failure = NULL;
    unsigned i;

    for (i = 0; i < sizeof pads / sizeof pads[0]; i++)
    {
        BASEPAGE *bp;

        held[i] = pads[i] ? (void *)Malloc(pads[i]) : NULL;
        bp = (BASEPAGE *)Pexec(PE_LOAD, "C:\\PIEPROBE.TOS", "", 0);
        if ((long)bp <= 0)
        {
            failure = "PE_LOAD of PIEPROBE.TOS failed";
            continue;
        }
        /* the segments follow the basepage back to back, so the sizes add up
         * to where the bss ends, wherever the loader put the image */
        if ((uintptr_t)bp->p_dbase != (uintptr_t)bp + sizeof(BASEPAGE) + bp->p_tlen
            || (uintptr_t)bp->p_bbase + bp->p_blen
               != (uintptr_t)bp + sizeof(BASEPAGE) + bp->p_tlen + bp->p_dlen + bp->p_blen)
            failure = "the sizes in the basepage do not reach the end of the image";
        if (Pexec(PE_GO_FREE, "", bp, 0) != 0)
            failure = "PIEPROBE.TOS exited nonzero after PE_LOAD";
    }
    for (i = 0; i < sizeof pads / sizeof pads[0]; i++)
        if (held[i])
            Mfree(held[i]);

    return failure;
}

#endif

void test_pie_load(void)
{
    long rc;

    ptest_begin("pie_load");

    rc = Pexec(PE_LOADGO, "C:\\PIEPROBE.TOS", "", 0);
    ptest_assert_msg(rc == 0,
                      "PIEPROBE.TOS exited nonzero "
                      "(PIE dynamic relocations not applied?)");

#if defined(TEST_WITH_USER_ASPACE) && !defined(TEST_WITH_SHARED_VM_PEXEC)
    {
        BASEPAGE *bp = (BASEPAGE *)Pexec(PE_LOAD, "C:\\PIEPROBE.TOS", "", 0);
        ptest_assert_msg((long)bp > 0, "mode 3 did not stage PIEPROBE.TOS");
        if ((long)bp > 0) {
            ptest_assert_msg(Pexec(PE_GO, "", bp, 0) == -32,
                             "ring-3 mode 4 was not rejected");
            ptest_assert_msg(Pexec(104, "", bp, 0) == -32,
                             "mode 104 was not rejected");
            ptest_assert_msg(bp->p_lowtpa == (char *)bp,
                             "rejected mode 4 altered its staged basepage");
            ptest_assert_msg(Pexec(PE_GO_FREE, "", bp, 0) == 0,
                             "mode 6 failed after rejected mode 4");
        }
        bp = (BASEPAGE *)Pexec(PE_LOAD, "C:\\PIEPROBE.TOS", "", 0);
        ptest_assert_msg((long)bp > 0, "second mode 3 staging failed");
        if ((long)bp > 0) {
            char *env = bp->p_env;
            ptest_assert_msg(Pexec(PE_GO, "", bp, 0) == -32,
                             "second mode 4 was not rejected");
            ptest_assert_msg(Mfree(bp) == 0 && Mfree(env) == 0,
                             "rejected staged image was not reclaimable");
        }
    }
#endif

#if (defined(__arm__) || defined(__mc68000__)) && !defined(TEST_WITH_USER_ASPACE)
    {
        const char *failure = check_footprint();

        ptest_assert_msg(!failure, failure ? failure : "");
    }
#endif

#if defined(TEST_WITH_SHARED_VM_PEXEC)
    shared_vm_basic();
    shared_vm_status();
    shared_vm_staged_retirement();
    shared_vm_orphan();
    shared_vm_limits();
#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_PTHJOIN)
    shared_vm_managed_worker();
#endif
#if defined(TEST_WITH_ELF_SHARED)
    shared_vm_interpreted();
    shared_vm_interpreted_peers();
#endif
#if defined(__mc68000__)
    shared_vm_prg();
#endif
#endif
    ptest_pass();
}
