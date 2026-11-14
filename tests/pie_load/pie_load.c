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
void shared_vm_run_managed_tests(void);
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#include "../../include/mman.h"
static void shared_vm_diagnostic(const char *label,long value)
{
    char number[11];unsigned i;
    for(i=0;i<8;i++) number[i]="0123456789abcdef"[((unsigned long)value>>(28-4*i))&15];
    number[8]='\r';number[9]='\n';number[10]=0;conws(label);conws(number);
}
static void shared_vm_tail(char *,char,struct shared_vm_probe *);
#include "../../include/shared_pexec_test.h"
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
#include "../../include/mapping_lifetime_test.h"
static void shared_vm_common_snapshot(MAPPING_LIFETIME_SNAPSHOT *out)
{
    ptest_assert_msg(trap_1_wll(GEMDOS_PCLONE,(long)out,MLT_SNAPSHOT)==0,
        "shared resources: canonical 45-counter snapshot");
}
static void shared_vm_common_equal(const MAPPING_LIFETIME_SNAPSHOT *a,const MAPPING_LIFETIME_SNAPSHOT *b)
{
    unsigned i;
    for(i=0;i<MLT_FIELDS;i++) {
        if(a->value[i]!=b->value[i]) {
            shared_vm_diagnostic("shared canonical resource field=",i);
            shared_vm_diagnostic("before=",a->value[i]);shared_vm_diagnostic("after=",b->value[i]);
        }
        ptest_assert_msg(a->value[i]==b->value[i],"shared interpreter: exact 45-counter steady-root conservation");
    }
}
static void shared_vm_snapshot(SHARED_PEXEC_SNAPSHOT *out)
{
    ptest_assert_msg(trap_1_wll(GEMDOS_PCLONE,(long)out,SHARED_PEXEC_SNAPSHOT_QUERY)==0,
        "shared resources: resident allocation-free snapshot");
}
static void shared_vm_snapshot_log(const char *boundary,const SHARED_PEXEC_SNAPSHOT *out)
{
    unsigned i;
    conws(boundary);conws("\r\n");
    for(i=0;i<SP_COUNT;i++) {
        shared_vm_diagnostic("resource index=",i);
        shared_vm_diagnostic("resource count=",(long)out->count[i]);
    }
}
static void shared_vm_conserved(const SHARED_PEXEC_SNAPSHOT *before,const SHARED_PEXEC_SNAPSHOT *after)
{
    unsigned i;
    for(i=0;i<SP_COUNT;i++) {
        /* Empty root page tables and grown owner vectors remain legitimate
         * root resources. Record them from the FIRST launch, and account
         * their exact live blocks instead of replacing/resetting a baseline. */
        if(i==SP_TABLE_PAGES || i==SP_VECTOR_BLOCKS || i==SP_VECTOR_BYTES ||
           i==SP_KHEAP_PAGES || i==SP_KHEAP_BLOCKS) continue;
        if(before->count[i]!=after->count[i]) {
            shared_vm_diagnostic("unconserved resource index=",i);
            shared_vm_diagnostic("before=",(long)before->count[i]);
            shared_vm_diagnostic("after=",(long)after->count[i]);
        }
        ptest_assert_msg(before->count[i]==after->count[i],"shared resources: exact final owner conservation");
    }
    ptest_assert_msg(after->count[SP_KHEAP_BLOCKS]-before->count[SP_KHEAP_BLOCKS]==
        after->count[SP_VECTOR_BLOCKS]-before->count[SP_VECTOR_BLOCKS],
        "shared resources: all remaining metadata blocks belong to recorded root vectors");
}
static void shared_vm_retirement(const SHARED_PEXEC_SNAPSHOT *before,unsigned survivors)
{
    static SHARED_PEXEC_SNAPSHOT current;
    unsigned spin;
    /* A user mailbox precedes Pterm; await actual stopped-THREAD and observer
     * cleanup rather than treating an arbitrary number of yields as exit. */
    for(spin=0;spin<512;spin++) {
        shared_vm_snapshot(&current);
        if(current.count[SP_THREADS]==before->count[SP_THREADS]+survivors &&
            current.count[SP_SCHEDSTACKS]==before->count[SP_SCHEDSTACKS]+survivors &&
            (survivors || (current.count[SP_RAW_HOLDS]==before->count[SP_RAW_HOLDS] &&
                          current.count[SP_RESERVATION_HOLDS]==before->count[SP_RESERVATION_HOLDS]))) break;
        (void)Syield();
    }
    if(spin>8) shared_vm_diagnostic("shared final retirement checkpoints=",spin);
    ptest_assert_msg(spin<512,"shared resources: actual THREAD/observer retirement reaches its baseline");
}
static void shared_vm_multiple_lifetime(void)
{
    static struct shared_vm_probe probes[3];
    static SHARED_PEXEC_SNAPSHOT snapshots[4];
    char tail[12];BASEPAGE *image;char *env;long pid,result;unsigned spin,n;
    memset(probes,0,sizeof(probes));memset(snapshots,0,sizeof(snapshots));
    shared_vm_snapshot(&snapshots[0]);
    shared_vm_tail(tail,'u',probes);
    image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
    ptest_assert_msg((long)image>0,"shared resources: stage multi-child source");
    if((long)image<=0) return;
    env=image->p_env;pid=Pexec(104,0,image,0);
    ptest_assert_msg(pid>0,"shared resources: publish independently managed source");
    if(pid>0) {
        for(spin=0;spin<512 && !probes[0].started && !probes[0].error;spin++) (void)Syield();
        ptest_assert_msg(probes[0].started && !probes[0].error && probes[1].child_pid &&
            probes[2].child_pid && probes[1].child_pid!=probes[2].child_pid,
            "shared resources: distinct PROCESSes with two simultaneously surviving children");
        shared_vm_snapshot(&snapshots[1]);
        ptest_assert_msg(snapshots[1].count[SP_RAW_HOLDS]>snapshots[0].count[SP_RAW_HOLDS] &&
            snapshots[1].count[SP_RESERVATION_HOLDS]>snapshots[0].count[SP_RESERVATION_HOLDS] &&
            snapshots[1].count[SP_THREADS]>=snapshots[0].count[SP_THREADS]+3,
            "shared resources: published PROCESS, THREAD, raw and logical owners counted");
        probes[0].gate=1;result=Pwaitpid(pid,0,0);
        ptest_assert_msg((result>>16)==pid && (result&65535)==0x32,
            "shared resources: parent completes before both live children");
        shared_vm_snapshot(&snapshots[2]);
        ptest_assert_msg(snapshots[2].count[SP_RAW_HOLDS]>snapshots[0].count[SP_RAW_HOLDS] &&
            snapshots[2].count[SP_RETIRED_HEAP]>=snapshots[0].count[SP_RETIRED_HEAP]+2,
            "shared resources: retired parent's exact stack births and raw owners remain retained");
        for(n=1;n<=2;n++) {
            ptest_assert_msg(Mfree(image)==-36 && Mfree(env)==-36,
                "shared resources: each remaining child independently holds source metadata");
            probes[n].gate=1;
            for(spin=0;spin<512 && !probes[n].done && !probes[n].error;spin++) (void)Syield();
            ptest_assert_msg(probes[n].done && probes[n].steps==1 && !probes[n].error,
                "shared resources: each orphan executes a grandchild after ancestor retirement");
            ptest_assert_msg(Pwaitpid(probes[n].child_pid,1,0)==-33,
                "shared resources: orphan observation creates no foreign reaping authority");
            shared_vm_retirement(&snapshots[0],2-n);
        }
    }
    {
        long image_result=Mfree(image),env_result=Mfree(env);
        if(image_result || env_result) {
            shared_vm_diagnostic("shared final image address=",(long)image);
            shared_vm_diagnostic("shared final image release=",image_result);
            shared_vm_diagnostic("shared final environment address=",(long)env);
            shared_vm_diagnostic("shared final environment release=",env_result);
        }
        ptest_assert_msg(image_result==0 && env_result==0,"shared resources: final allocation release before root destruction");
    }
    shared_vm_snapshot(&snapshots[3]);
    for(n=0;n<4;n++) shared_vm_snapshot_log(n==0?"SHARED before creation":n==1?
        "SHARED after publication":n==2?"SHARED after first PROCESS exit":"SHARED after final owner and observer",&snapshots[n]);
    shared_vm_conserved(&snapshots[0],&snapshots[3]);
    /* Reuse the actual released source PD and reject its old PID. */
    {
        BASEPAGE *again=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS","",0);
        ptest_assert_msg((long)again>0,"shared resources: stage after source PD retirement");
        if((long)again>0) {
            char *again_env=again->p_env;
            ptest_assert_msg(again==image,"shared resources: retired source PD address is actually reused");
            ptest_assert_msg(Pwaitpid(pid,1,0)==-33 && Pexec(4,0,again,0)==0,
                "shared resources: reused PD has fresh authority and retired PID is not a child");
            ptest_assert_msg(Mfree(again)==0 && Mfree(again_env)==0,"shared resources: reused incarnation final release");
        }
    }
    conws("native shared-VM four-boundary resource conservation and multiple surviving orphans: tested\r\n");
}
#endif
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
static void shared_vm_interpreted_orphan(void)
{
    static struct shared_vm_probe probes[2];
    BASEPAGE *image;char tail[12],*env;long pid,result;unsigned spin;
    memset(probes,0,sizeof(probes));shared_vm_tail(tail,'e',probes);
    image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
    ptest_assert_msg((long)image>0,"shared interpreter orphan: stage source");if((long)image<=0) return;
    env=image->p_env;pid=Pexec(104,0,image,0);
    ptest_assert_msg(pid>0,"shared interpreter orphan: source PID");
    if(pid>0) {
        for(spin=0;spin<512 && !probes[0].started && !probes[0].error;spin++) (void)Syield();
        ptest_assert_msg(probes[0].started && !probes[0].error,"shared interpreter orphan: independent child publication");
        probes[0].gate=1;result=Pwaitpid(pid,0,0);
        ptest_assert_msg((result>>16)==pid && (result&65535)==0x32,"shared interpreter orphan: source completes first");
        for(spin=0;spin<512 && !probes[1].started && !probes[1].error;spin++) (void)Syield();
        ptest_assert_msg(probes[1].started && !probes[1].done && !probes[1].error,
            "shared interpreter orphan: relocated interpreter remains runnable after source retirement");
        ptest_assert_msg(Mfree(image)==-36 && Mfree(env)==-36,"shared interpreter orphan: metadata retained by live interpreter");
        probes[1].gate=1;
        for(spin=0;spin<512 && !probes[1].done && !probes[1].error;spin++) (void)Syield();
        for(spin=0;spin<8;spin++) (void)Syield();
        ptest_assert_msg(probes[1].done && !probes[1].error,
            "shared interpreter orphan: normal main, AUXV/RELRO and finalizers survive source death");
        ptest_assert_msg(Pwaitpid(probes[1].child_pid,1,0)==-33,"shared interpreter orphan: independent orphan completion");
    }
    ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared interpreter orphan: final original owner release");
    conws("native shared-VM parent-first interpreter execution and finalization: tested\r\n");
}
static void shared_vm_interpreted(void)
{
    static const char envspec[]="CHECK=phase5\0\0";
    unsigned round,mode,fault;
    /* Preserve the first two original launches and account their root-vector
     * growth; then require all 45 counters equal for 128 further executions.
     * Resource counters are never reset and the parent root remains alive. */
    for(round=0;round<130;round++) {
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
        static SHARED_PEXEC_SNAPSHOT before,after;
        static MAPPING_LIFETIME_SNAPSHOT common_before,common_after;
#endif
        BASEPAGE *bp;
        char *env;long rc;
        mode=round&1;fault=round>=2 && ((round-2)&2);
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
        memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
        shared_vm_snapshot(&before);
        memset(&common_before,0,sizeof(common_before));memset(&common_after,0,sizeof(common_after));
        shared_vm_common_snapshot(&common_before);
#endif
        bp=(BASEPAGE *)Pexec(PE_LOAD,"C:\\SHRINT.ELF",fault?"\5fault":"",envspec);
        if((long)bp<=0) shared_vm_interpreted_result(2+mode,(long)bp);
        ptest_assert_msg((long)bp>0,"shared interpreter: PE_LOAD");if((long)bp<=0) continue;
        env=bp->p_env;rc=Pexec(mode?104:4,0,bp,0);
        if(round<2 || rc<0) shared_vm_interpreted_result(mode,rc);
        if(rc<0) {conws("shared interpreter launch error\r\n");}
        ptest_assert_msg(mode?rc>0:rc==(fault?0x0400:77),"shared interpreter: auxv, relocation, RELRO and normal/fault completion");
        if(mode && rc>0) {
            long waited=Pwaitpid(rc,0,0);
            if(round<2 || (waited&65535)!=(fault?0x0400:77)) shared_vm_interpreted_result(mode,waited);
            ptest_assert_msg((waited>>16)==rc && (waited&65535)==(fault?0x0400:77),"shared interpreter: canonical normal/fault PROCESS wait");
            ptest_assert_msg(Pwaitpid(rc,1,0)==-33,"shared interpreter: exactly one reap");
        }
        ptest_assert_msg(Mfree(bp)==0,"shared interpreter: final image cleanup");
        ptest_assert_msg(Mfree(env)==0,"shared interpreter: final environment cleanup");
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
        shared_vm_snapshot(&after);
        shared_vm_common_snapshot(&common_after);
        if(round<2) {
            shared_vm_diagnostic("shared interpreter current-root regions before=",(long)before.count[SP_CURRENT_MMAP]);
            shared_vm_diagnostic("shared interpreter current-root regions after=",(long)after.count[SP_CURRENT_MMAP]);
            shared_vm_diagnostic("shared interpreter current-root backing before=",(long)before.count[SP_CURRENT_PRIVATE]);
            shared_vm_diagnostic("shared interpreter current-root backing after=",(long)after.count[SP_CURRENT_PRIVATE]);
        }
        shared_vm_conserved(&before,&after);
        if(round>=2) shared_vm_common_equal(&common_before,&common_after);
#endif
    }
    conws("native shared-VM 128 interpreted Pexec4/104 normal/fault-before-fini exact 45-counter conservation: tested\r\n");
    conws("native shared-VM PT_INTERP Pexec4/104 standard entry: tested\r\n");
}
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void shared_vm_dso_retention(void)
{
    static struct shared_vm_probe probe;
    static SHARED_PEXEC_SNAPSHOT snapshots[4];
    static MAPPING_LIFETIME_SNAPSHOT common[4];
    static MAPPING_LIFETIME_SNAPSHOT descendant;
    static const unsigned char late_file[4096]={0x7f,'E','L','F'};
    long late_fd;
    unsigned fault,spin,boundary;
    /* Use a fresh file: the ELF loader already cached SHRINT's first page on
     * ARM/m68k, so that page cannot prove a new post-exit lazy file fault. */
    late_fd=Fcreate("C:\\DSOLATE.BIN",0);
    ptest_assert_msg(late_fd>=0,"shared DSO late runtime: create uncached file");
    if(late_fd<0) return;
    ptest_assert_msg(Fwrite(late_fd,sizeof(late_file),late_file)==sizeof(late_file),
        "shared DSO late runtime: complete file payload");
    ptest_assert_msg(Fclose(late_fd)==0,"shared DSO late runtime: close file before mapping");
    for(fault=0;fault<2;fault++) {
        BASEPAGE *image;
        char tail[12],*env;
        void *stack=(void *)Malloc(32768);
        long pid,result;
        ptest_assert_msg((long)stack>0,"shared DSO peer: initial stack");if((long)stack<=0) return;
        memset(stack,0,32768);memset(&probe,0,sizeof(probe));
        memset(snapshots,0,sizeof(snapshots));memset(common,0,sizeof(common));
        probe.worker_stack=(uintptr_t)stack;
        shared_vm_snapshot(&snapshots[0]);shared_vm_common_snapshot(&common[0]);
        shared_vm_tail(tail,fault?'q':'d',&probe);
        image=(BASEPAGE *)Pexec(3,"C:\\SHRINT.ELF",tail,"CHECK=phase5\0\0");
        ptest_assert_msg((long)image>0,"shared DSO peer: actual interpreted PE_LOAD");
        if((long)image<=0) { (void)Mfree(stack);continue; }
        env=image->p_env;pid=Pexec(104,0,image,0);
        ptest_assert_msg(pid>0,"shared DSO peer: distinct creator PROCESS PID");
        if(pid>0) {
            for(spin=0;spin<512 && !probe.started && !probe.error;spin++) (void)Syield();
            ptest_assert_msg(probe.started && probe.worker_started && probe.steps==1 && !probe.error,
                "shared DSO peer: generic peer executes actual DSO code/data before creator termination");
            if(probe.error) shared_vm_diagnostic("shared DSO peer construction error=",probe.error);
            ptest_assert_msg(probe.second_tid>16 && probe.same_limit==probe.second_tid,
                "shared DSO peer: every special dependency prefix rolls back without publication or a resource delta");
            shared_vm_diagnostic("shared DSO native dependency failure prefixes=",probe.second_tid);
            shared_vm_snapshot(&snapshots[1]);shared_vm_common_snapshot(&common[1]);
            ptest_assert_msg(common[1].value[MLT_MMAP]>common[0].value[MLT_MMAP] &&
                common[1].value[MLT_MEMBERS]>=common[0].value[MLT_MEMBERS]+3,
                "shared DSO peer: published runtime mappings and independent PROCESS owners counted");
            probe.resize_limit=1;result=Pwaitpid(pid,0,0);
            ptest_assert_msg((result>>16)==pid && (result&65535)==(fault?0x0400:77),
                "shared DSO peer: creator normal/fault-before-fini completion uses canonical reaper");
            ptest_assert_msg(Pwaitpid(pid,1,0)==-33,"shared DSO peer: creator is reaped exactly once");
            shared_vm_snapshot(&snapshots[2]);shared_vm_common_snapshot(&common[2]);
            ptest_assert_msg(common[2].value[MLT_MMAP]==common[1].value[MLT_MMAP] &&
                common[2].value[MLT_BACKING_REFS]==common[1].value[MLT_BACKING_REFS],
                "shared DSO peer: creator exit cannot revoke the remaining peer's code or writable data");
            ptest_assert_msg(Mfree(image)==-36 && Mfree(env)==-36,
                "shared DSO peer: independent peer retains exact executable/basepage/environment dependencies");
            ptest_assert_msg(probe.grandchild && *(volatile uint32_t *)(uintptr_t)probe.grandchild==0x644894UL,
                "shared DSO peer: future ordinary allocation survives its departed creator");
            probe.gate=1;
            for(spin=0;spin<512 && !probe.done && !probe.error;spin++) (void)Syield();
            ptest_assert_msg(probe.done && probe.steps==2 && !probe.error,
                "shared DSO peer: executes file-backed DSO after creator exit and writes surviving data");
            if(probe.error) shared_vm_diagnostic("shared DSO descendant launch result=",probe.environment);
            ptest_assert_msg(probe.descendant_mapping_prefixes>4 && probe.descendant_preparation_prefixes>16,
                "shared DSO descendant: every mapping/R1/R2 preparation prefix preserves the staged image and live source");
            shared_vm_diagnostic("shared DSO loaded mapping failure prefixes=",probe.descendant_mapping_prefixes);
            shared_vm_diagnostic("shared DSO loaded preparation failure prefixes=",probe.descendant_preparation_prefixes);
            ptest_assert_msg(Pwaitpid(probe.child_pid,1,0)==-33,
                "shared DSO peer: unrelated observer gains no orphan reaping authority");
            shared_vm_retirement(&snapshots[0],1);
            shared_vm_common_snapshot(&descendant);
            ptest_assert_msg(probe.descendant_ready && !probe.descendant_done && !probe.error &&
                descendant.value[MLT_ALLOC_HOLDS]==common[0].value[MLT_ALLOC_HOLDS]+8,
                "shared DSO descendant: complete ancestor and immediate-source R1 prefix survives both creators");
            if(descendant.value[MLT_MMAP]!=common[2].value[MLT_MMAP] ||
                descendant.value[MLT_BACKING_REFS]!=common[2].value[MLT_BACKING_REFS]+1 ||
                descendant.value[MLT_FILE_PAGES]!=common[2].value[MLT_FILE_PAGES]+1) {
                shared_vm_diagnostic("shared DSO creator-exit regions=",common[2].value[MLT_MMAP]);
                shared_vm_diagnostic("shared DSO descendant regions=",descendant.value[MLT_MMAP]);
                shared_vm_diagnostic("shared DSO creator-exit backing refs=",common[2].value[MLT_BACKING_REFS]);
                shared_vm_diagnostic("shared DSO descendant backing refs=",descendant.value[MLT_BACKING_REFS]);
                shared_vm_diagnostic("shared DSO creator-exit file pages=",common[2].value[MLT_FILE_PAGES]);
                shared_vm_diagnostic("shared DSO descendant file pages=",descendant.value[MLT_FILE_PAGES]);
            }
            /* The peer first faults the late file page after creator exit,
             * then COWs it. Its clean cache page owns one backing reference
             * while the descendant retains the canonical file reservation. */
            ptest_assert_msg(descendant.value[MLT_MMAP]==common[2].value[MLT_MMAP] &&
                descendant.value[MLT_BACKING_REFS]==common[2].value[MLT_BACKING_REFS]+1 &&
                descendant.value[MLT_FILE_PAGES]==common[2].value[MLT_FILE_PAGES]+1,
                "shared DSO descendant: loaded exact runtime attachments remain after generic peer exit");
            ptest_assert_msg(Mfree(image)==-36 && Mfree(env)==-36,
                "shared DSO descendant: original metadata remain held after generic peer exit");
            ptest_assert_msg(probe.unrelated_runtime &&
                trap_1_wlll(GEMDOS_MPROT,probe.unrelated_runtime,4096,PROT_READ|PROT_WRITE)==-40,
                "shared DSO descendant: later unexported runtime mapping is not implicitly attached by visibility");
            {
                unsigned char byte=0;
                long fd=Fopen("C:\\DSOLATE.BIN",0);
                ptest_assert_msg(fd>=0 && Fread(fd,1,&byte)==1 && byte==0x7f,
                    "shared DSO late runtime COW preserves the independent file contents");
                if(fd>=0) ptest_assert_msg(Fclose(fd)==0,"shared DSO late runtime independent file close");
            }
            probe.descendant_gate=1;
            for(spin=0;spin<512 && !probe.descendant_done && !probe.error;spin++) (void)Syield();
            if(probe.error) shared_vm_diagnostic("shared DSO descendant error=",probe.error);
            ptest_assert_msg(probe.descendant_done && !probe.error,
                "shared DSO descendant: executes retained DSO/main code and accesses late runtime COW after both creators exit");
            ptest_assert_msg(Pwaitpid(probe.descendant_pid,1,0)==-33,
                "shared DSO descendant: orphan observation grants no public reap");
            shared_vm_retirement(&snapshots[0],0);
        }
        ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared DSO peer: final staged metadata release");
        ptest_assert_msg(probe.grandchild && Mfree((void *)(uintptr_t)probe.grandchild)==0,
                "shared DSO peer: surviving common owner explicitly frees its ordinary allocation");
        shared_vm_snapshot(&snapshots[3]);shared_vm_common_snapshot(&common[3]);
        for(boundary=0;boundary<4;boundary++) {
            shared_vm_diagnostic("shared DSO lifetime boundary=",boundary);
            shared_vm_diagnostic("common regions=",common[boundary].value[MLT_MMAP]);
            shared_vm_diagnostic("common members=",common[boundary].value[MLT_MEMBERS]);
            shared_vm_diagnostic("file backing refs=",common[boundary].value[MLT_BACKING_REFS]);
        }
        shared_vm_conserved(&snapshots[0],&snapshots[3]);
        shared_vm_common_equal(&common[0],&common[3]);
        ptest_assert_msg(Mfree(stack)==0,"shared DSO peer: final stopped stack release");
    }
    ptest_assert_msg(Fdelete("C:\\DSOLATE.BIN")==0,"shared DSO late runtime: unlink after final owner");
    conws("native shared-VM generic DSO peer creator-first normal/fault lifetime and exact 45-counter conservation: tested\r\n");
    conws("native shared-VM generic executable dependency prefixes, surviving owner rollback and retry: tested\r\n");
    conws("native shared-VM late runtime file-COW and loaded descendant after both creators retire: tested\r\n");
    conws("native shared-VM loaded runtime dependencies and ancestor R1 prefixes rollback/retry: tested\r\n");
}
#endif
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
                long waited;
                ptest_assert_msg(Pwaitpid(rc,0,(long *)bp->p_tbase)==-40,
                    "shared104: read-only usage copyout does not consume completion");
                waited=Pwaitpid(rc,0,0);
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
#if defined(TEST_WITH_MMAP)
static void shared_vm_file_mapping(void)
{
    static struct shared_vm_probe probe;static uint32_t contents[1024];
    long fd,first,second,result;BASEPAGE *image;void *env;char tail[12];
    contents[0]=0x12345678UL;
    fd=Fcreate("C:\\SHRMEM.BIN",0);
    ptest_assert_msg(fd>=0,"shared file: create backing");if(fd<0) return;
    ptest_assert_msg(Fwrite(fd,sizeof(contents),contents)==sizeof(contents) && Fclose(fd)==0,
        "shared file: complete backing contents");
    fd=Fopen("C:\\SHRMEM.BIN",0);ptest_assert_msg(fd>=0,"shared file: open mapping source");if(fd<0) return;
    first=trap_1_wllllll(GEMDOS_MMAP,0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    second=trap_1_wllllll(GEMDOS_MMAP,0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    ptest_assert_msg(Fclose(fd)==0,"shared file: caller descriptor closes before launch");
    ptest_assert_msg(first>0 && second>0,"shared file: independent file reservations");
    if(first>0 && second>0) {
        ptest_assert_msg(*(volatile uint32_t *)(uintptr_t)first==0x12345678UL,
            "shared file: resident clean file-COW witness");
        memset(&probe,0,sizeof(probe));probe.staged=(uint32_t)first;probe.environment=(uint32_t)second;
        shared_vm_tail(tail,'m',&probe);image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
        ptest_assert_msg((long)image>0,"shared file: stage child");
        if((long)image>0) {
            env=image->p_env;result=Pexec(4,0,image,0);
            shared_vm_diagnostic("shared file child status=",result);
            ptest_assert_msg(result==0x34 && probe.done && !probe.error,
                "shared file: child materializes absent file page and writes file-COW");
            ptest_assert_msg(*(volatile uint32_t *)(uintptr_t)first==0x89400001UL &&
                *(volatile uint32_t *)(uintptr_t)second==0x89400002UL,
                "shared file: shared ASPACE observes both private file-COW writes");
            ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared file: reclaim child image");
        }
    }
    if(first>0) ptest_assert_msg(trap_1_wll(GEMDOS_MUNMAP,first,4096)==0,"shared file: retire first mapping");
    if(second>0) ptest_assert_msg(trap_1_wll(GEMDOS_MUNMAP,second,4096)==0,"shared file: retire second mapping");
    fd=Fopen("C:\\SHRMEM.BIN",0);
    ptest_assert_msg(fd>=0,"shared file: backing survives descriptor retirement");
    if(fd>=0) {
        uint32_t word=0;
        ptest_assert_msg(Fread(fd,sizeof(word),&word)==sizeof(word) && word==0x12345678UL,
            "shared file: private writes never modify backing file");
        ptest_assert_msg(Fclose(fd)==0,"shared file: close verification descriptor");
    }
    ptest_assert_msg(Fdelete("C:\\SHRMEM.BIN")==0,"shared file: final backing cleanup");
    conws("native shared-VM absent file backing and resident file-COW: tested\r\n");
}
#endif

static void shared_vm_relaunch_rejection(void)
{
    static struct shared_vm_probe probe;
    BASEPAGE *image;void *env;char tail[12];long pid,result;unsigned spin;
    memset(&probe,0,sizeof(probe));shared_vm_tail(tail,'r',&probe);
    image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
    ptest_assert_msg((long)image>0,"shared relaunch: stage target");if((long)image<=0) return;
    env=image->p_env;pid=Pexec(104,0,image,0);
    ptest_assert_msg(pid>0,"shared relaunch: publish original live child");
    if(pid>0) {
        ptest_assert_msg(Pexec(4,0,image,0)==-40 && Pexec(104,0,image,0)==-40,
            "shared relaunch: reject both modes without aborting live child's set");
        for(spin=0;spin<512 && !probe.started && !probe.error;spin++) (void)Syield();
        ptest_assert_msg(probe.started && !probe.error && !probe.done &&
            (int32_t)probe.controller_result==-40 && (int32_t)probe.resize_limit==-40,
            "shared relaunch: live child rejects self launch and remains runnable");
        ptest_assert_msg(Pwaitpid(pid,1,0)==0,"shared relaunch: failed launch does not fabricate completion");
        probe.gate=1;result=Pwaitpid(pid,0,0);
        ptest_assert_msg((result>>16)==pid && (result&65535)==0x33 && probe.done && !probe.error,
            "shared relaunch: original child still executes and completes");
        ptest_assert_msg(Pwaitpid(pid,1,0)==-33,"shared relaunch: original completion reaped once");
    }
    ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared relaunch: original owner finally frees image");
    conws("native shared-VM live-target and self-relaunch ownership rejection: tested\r\n");
}

#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void shared_vm_preparation_sweep(void)
{
    unsigned interpreted;
    for(interpreted=0;interpreted<
#if defined(TEST_WITH_ELF_SHARED)
        2
#else
        1
#endif
        ;interpreted++) {
        unsigned step;long last=0;
        for(step=1;step<256;step++) {
            static SHARED_PEXEC_SNAPSHOT before,after;
            BASEPAGE saved,*image=(BASEPAGE *)Pexec(3,
                interpreted?"C:\\SHRINT.ELF":"C:\\PIEPROBE.TOS","","CHECK=phase5\0\0");
            char *env;long result,seen;
            ptest_assert_msg((long)image>0,"shared failure sweep: original authenticated PE_LOAD");
            if((long)image<=0) break;
            env=image->p_env;saved=*image;
            memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
            shared_vm_snapshot(&before);
            ptest_assert_msg(trap_1_wll(GEMDOS_PCLONE,0,0xc8940900UL+step)==0,
                "shared failure sweep: inject next exact preparation prefix");
            result=Pexec(step&1?4:104,0,image,0);
            seen=trap_1_wll(GEMDOS_PCLONE,0,0xc8940a00UL);last=seen;
            (void)trap_1_wll(GEMDOS_PCLONE,0,0xc8940900UL);
            if(seen<(long)step && result>=0) {
                if(!(step&1) && result>0) {
                    long waited=Pwaitpid(result,0,0);
                    ptest_assert_msg((waited>>16)==result && (waited&65535)==(interpreted?77:0),
                        "shared failure sweep: final noninjected asynchronous publication");
                } else ptest_assert_msg(result==(interpreted?77:0),
                    "shared failure sweep: final noninjected synchronous publication");
                ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared failure sweep: final target release");
                break;
            }
            ptest_assert_msg(result<0 && seen==(long)step,
                "shared failure sweep: injected prefix fails before runnable publication");
            shared_vm_snapshot(&after);shared_vm_conserved(&before,&after);
            ptest_assert_msg(!memcmp(&saved,image,sizeof(saved)) && image->p_env==env,
                "shared failure sweep: original basepage/environment and startup recipe preserved");
            ptest_assert_msg(Pwaitpid(-1,1,0)==-33,
                "shared failure sweep: no unexpected completion, stale controller or public child");
            result=Pexec(4,0,image,0);
            if(result!=(interpreted?77:0)) {
                shared_vm_diagnostic("shared failed retry prefix=",step);
                shared_vm_diagnostic("shared failed retry result=",result);
            }
            ptest_assert_msg(result==(interpreted?77:0),
                "shared failure sweep: same original image executes after complete rollback");
            ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared failure sweep: caller final ownership release");
            if(result!=(interpreted?77:0)) break; /* preserve failure; later prefixes cannot run */
        }
        ptest_assert_msg(step<256 && last>16,"shared failure sweep: all finite meaningful acquisition prefixes executed");
        shared_vm_diagnostic(interpreted?"shared interpreted preparation points=":"shared static preparation points=",last);
    }
    conws("native shared-VM exhaustive preparation prefixes and exact rollback conservation: tested\r\n");
}
static void shared_vm_constructor_rollback(void)
{
    unsigned asynchronous,stage;
    for(asynchronous=0;asynchronous<2;asynchronous++) for(stage=1;stage<=6;stage++) {
        BASEPAGE *image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS","",0);
        void *env,*entry;long result,control;
        ptest_assert_msg((long)image>0,"shared rollback: original caller-owned PE_LOAD");
        if((long)image<=0) continue;
        env=image->p_env;entry=image->p_tbase;
        ptest_assert_msg(Pwaitpid(-1,1,0)==-33,"shared rollback: no public child before injection");
        control=stage==6?0xc8920600UL:0xc8920400UL+stage;
        ptest_assert_msg(trap_1_wll(GEMDOS_PCLONE,0,control)==0,"shared rollback: arm existing private factory injection");
        result=Pexec(asynchronous?104:4,0,image,0);
        if(result!=(stage==6?-326:-39)) shared_vm_diagnostic("shared rollback unexpected result=",result);
        ptest_assert_msg(result==(stage==6?-326:-39),"shared rollback: exact preparation error before publication");
        (void)trap_1_wll(GEMDOS_PCLONE,0,0xc8920400UL);
        ptest_assert_msg(image->p_env==env && image->p_tbase==entry &&
            Mvalidate(0,image,256,0)==0 && Mvalidate(0,entry,1,0)==0,
            "shared rollback: staged image and compatibility ownership preserved");
        ptest_assert_msg(Pwaitpid(-1,1,0)==-33,"shared rollback: no uncommitted PROCESS completion");
        result=Pexec(asynchronous?104:4,0,image,0);
        if(asynchronous && result>0) {
            long pid=result;result=Pwaitpid(pid,0,0);
            ptest_assert_msg((result>>16)==pid && !(result&65535),"shared rollback: same staged child executes after retry");
            ptest_assert_msg(Pwaitpid(pid,1,0)==-33,"shared rollback: retry reaped once");
        } else ptest_assert_msg(!asynchronous && result==0,"shared rollback: synchronous staged retry executes");
        ptest_assert_msg(Mfree(image)==0 && Mfree(env)==0,"shared rollback: original owner finally reclaims image/env");
    }
    conws("native shared-VM constructor/observer/prepublication rollback and retry: tested\r\n");
}
#endif

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

static void shared_vm_orphan(char role)
{
    static struct shared_vm_probe probe;
    char tail[12];BASEPAGE *image;char *env;long result;unsigned spin;
    memset(&probe,0,sizeof(probe));shared_vm_tail(tail,role,&probe);
    image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
    ptest_assert_msg((long)image>0,"shared orphan: source PE_LOAD");if((long)image<=0) return;
    env=image->p_env;result=Pexec(4,0,image,0);
    ptest_assert_msg(result==(role=='g'?0x63:0x32) && probe.child_pid && !probe.error,
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
    ptest_assert_msg(probe.done && probe.steps==1 && !probe.error,
        "shared orphan: runnable orphan executes and reaps a new shared PROCESS after source retirement");
    if(probe.staged) ptest_assert_msg(Mvalidate(0,(void *)(uintptr_t)probe.staged,256,0)==-25 &&
        Mvalidate(0,(void *)(uintptr_t)probe.environment,1,0)==-25,
        "shared orphan: child allocations reclaimed before shared root dies");
    if(probe.grandchild) ptest_assert_msg(Mvalidate(0,(void *)(uintptr_t)probe.grandchild,256,0)==-25 &&
        Mvalidate(0,(void *)(uintptr_t)probe.grand_environment,1,0)==-25,
        "shared orphan: executed grandchild allocations reclaimed exactly once");
    ptest_assert_msg(Pwaitpid(probe.child_pid,1,0)==-33,"shared orphan: unrelated PROCESS cannot consume an orphan result");
    ptest_assert_msg(Mfree(image)==0,"shared orphan: source image final release");
    ptest_assert_msg(Mfree(env)==0,"shared orphan: source environment final release");
    if(role=='g') {
        ptest_assert_msg(probe.worker_started && probe.steps==1 && !probe.second_tid,
            "shared group: cancellation stops own worker without running its continuation");
        if(probe.worker_stack) ptest_assert_msg(Mfree((void *)(uintptr_t)probe.worker_stack)==0,
            "shared group: stopped worker stack released after independent child");
        conws("native shared-VM PROCESS-local group cancellation and orphan survival: tested\r\n");
    }
    conws("native shared-VM running orphan and nested unstarted cleanup: tested\r\n");
    conws("native shared-VM orphan grandchild execution after source retirement: tested\r\n");
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
        if(!asynchronous) {
            long cap=Psetlimit(0x100,-1);
            unsigned long distance=cap>32768?32768:((unsigned long)cap/2)&~4095UL;
            volatile uint32_t *deep=(volatile uint32_t *)(uintptr_t)(0x80000000UL-distance);
            ptest_assert_msg(cap>=8192 && distance>=4096,
                "shared THREAD lifetime: probe lies inside the actual inherited stack cap");
            ptest_assert_msg(Mvalidate(0,(void *)deep,4,0)==0,
                "shared THREAD lifetime: first sibling preserves original lazy top-stack ownership");
            *deep=0x894966UL;
            ptest_assert_msg(*deep==0x894966UL,"shared THREAD lifetime: untouched original stack page still materializes");
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
#if defined(TEST_WITH_PRIVATE_CLONE_TEST) && defined(TEST_WITH_PTERMGRP)
static SHARED_PEXEC_PROTECTED shared_protected;
static struct shared_vm_probe shared_origin;
static void shared_vm_cancel_peer(void *argument)
{
    (void)argument;
    /* First dispatch occurs after the protected mode4 caller has blocked. */
    (void)trap_1_ww(GEMDOS_PTERMGRP,0x64);
    Pterm(95);
}
static void shared_vm_clone_source(void *argument)
{
    struct shared_vm_probe *probe=argument;unsigned mode;char tail[12];long tid;
    char environment[2]={0,0};
    probe->pid=Pgetpid();tid=trap_1_wll(GEMDOS_PCLONE,0,0xc8920103UL);
    if(Psetlimit(0x100,-1)!=1024UL*1024 || Mvalidate(0,(void *)0x7f001000UL,4,0)!=-25) {
        probe->error=90;return;
    }
    for(mode=0;mode<3;mode++) {
        BASEPAGE *image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS","",environment);char *env;long result;
        if((long)image<=0) {probe->controller_result=(uint32_t)(long)image;probe->error=91;return;}
        env=image->p_env;result=Pexec(104,0,image,0);
        if(result<=0 || Pwaitpid(result,1,0)!=0) {probe->error=92;return;}
        if(mode==0) result=Pwait();
        else if(mode==1) result=Pwait3(0,0);
        else {unsigned spin;for(spin=0;spin<8;spin++) (void)Syield();result=Pwaitpid(result,0,0);}
        if((result&65535) || (result>>16)<=0 || Mfree(image) || Mfree(env)) {probe->error=93;return;}
    }
    shared_vm_tail(tail,'n',probe);
    if(Pexec(0,"C:\\PIEPROBE.TOS",tail,environment)!=0x35 || probe->error ||
       trap_1_wll(GEMDOS_PCLONE,0,0xc892070aUL)==Pgetpid() ||
       trap_1_wll(GEMDOS_PCLONE,0,0xc892070bUL)==tid ||
       trap_1_wll(GEMDOS_PCLONE,0,0xc8920103UL)!=tid) {probe->error=94;return;}
    {
        BASEPAGE *image=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS","\1f",environment);
        struct ptos_clone_args args={0};void *stack=(void *)Malloc(16384);
        if((long)image<=0 || (long)stack<=0) {probe->error=95;return;}
        probe->worker_stack=(uint32_t)(uintptr_t)stack;
        args.flags=CLONE_VM|CLONE_SAME_PROCESS;args.entry=shared_vm_cancel_peer;
        args.stack_base=stack;args.stack_size=16384;
        if(trap_1_wll(GEMDOS_PCLONE,(long)&args,sizeof(args))<=0) {probe->error=96;return;}
        shared_protected.image=(uint32_t)(uintptr_t)image;
        (void)trap_1_wll(GEMDOS_PCLONE,(long)&shared_protected,SHARED_PEXEC_PROTECTED_QUERY);
        probe->error=97; /* enclosing safe checkpoint must not return */
    }
}
static void shared_vm_source_origins(void)
{
    struct ptos_clone_args args={0};void *stack;long pid,result,limit;
    int32_t process_pid=-1;
    static SHARED_PEXEC_SNAPSHOT before,after;
    memset(&shared_origin,0,sizeof(shared_origin));memset(&shared_protected,0,sizeof(shared_protected));
    memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
    limit=Psetlimit(0x100,1024UL*1024);
    shared_vm_snapshot(&before);
    stack=(void *)Malloc(32768);
    ptest_assert_msg((long)stack>0,"shared origins: generic PROCESS initial stack");if((long)stack<=0) return;
    args.flags=CLONE_VM|CLONE_COPY_STANDARD_HANDLES|CLONE_COPY_CWD;
    args.entry=shared_vm_clone_source;args.argument=&shared_origin;
    args.stack_base=stack;args.stack_size=32768;
    args.child_pid=&process_pid;
    result=trap_1_wll(GEMDOS_PCLONE,(long)&args,sizeof(args));
    pid=result>0?process_pid:result;
    ptest_assert_msg(pid>0,"shared origins: independent generic Pclone PROCESS");
    if(pid>0) {
        result=Pwaitpid(pid,0,0);
        shared_vm_diagnostic("shared source-origin completion=",result);
        shared_vm_diagnostic("shared source-origin error=",shared_origin.error);
        shared_vm_diagnostic("shared source-origin load result=",shared_origin.controller_result);
        shared_vm_diagnostic("shared protected child result=",shared_protected.result);
        shared_vm_diagnostic("shared protected cancellation pending=",shared_protected.pending);
        ptest_assert_msg((result>>16)==pid && (result&65535)==0x64 && !shared_origin.error,
            "shared origins: canonical waits, nested source launches and protected cancellation complete");
        ptest_assert_msg(shared_protected.result==65535 && shared_protected.pending &&
            shared_protected.tid_before && shared_protected.tid_before==shared_protected.tid_after,
            "shared cancellation: full child result and authentic owner restored before pending group exit");
        ptest_assert_msg(Pwaitpid(pid,1,0)==-33,"shared cancellation: exactly one final PROCESS completion");
    }
    if(shared_origin.worker_stack) ptest_assert_msg(Mfree((void *)(uintptr_t)shared_origin.worker_stack)==0,
        "shared origins: cancelled peer's user stack released after its final THREAD");
    ptest_assert_msg(Mfree(stack)==0,"shared origins: original source stack reclaimed");
    shared_vm_snapshot(&after);shared_vm_conserved(&before,&after);
    (void)Psetlimit(0x100,limit);
    conws("native shared-VM Pclone and nested source convergence, wait ABIs and protected cancellation: tested\r\n");
}
#endif
void shared_vm_run_managed_tests(void)
{
    ptest_begin("shared-VM managed THREAD lifetime");
    shared_vm_last_thread();
#if defined(TEST_WITH_PRIVATE_CLONE_TEST) && defined(TEST_WITH_PTERMGRP)
    shared_vm_source_origins();
#endif
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
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    ptest_assert_msg(trap_1_wll(GEMDOS_PCLONE,0,SHARED_PEXEC_MOVE_QUERY)==0,
        "shared resources: production kernel memmove preserves every overlapping byte");
    conws("native shared-VM production kernel memmove overlap: tested\r\n");
    shared_vm_multiple_lifetime();
#endif
    shared_vm_basic();
    shared_vm_status();
    shared_vm_staged_retirement();
    shared_vm_orphan('o');
#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_PTHJOIN) && defined(TEST_WITH_PTERMGRP)
    shared_vm_orphan('g');
#endif
    shared_vm_limits();
#if defined(TEST_WITH_MMAP)
    shared_vm_file_mapping();
#endif
    shared_vm_relaunch_rejection();
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    shared_vm_constructor_rollback();
#endif
#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_PTHJOIN)
    shared_vm_managed_worker();
#endif
#if defined(TEST_WITH_ELF_SHARED)
    shared_vm_interpreted();
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    shared_vm_dso_retention();
#endif
    shared_vm_interpreted_peers();
    shared_vm_interpreted_orphan();
#endif
#if defined(__mc68000__)
    shared_vm_prg();
#endif
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    shared_vm_preparation_sweep();
#endif
#endif
    ptest_pass();
}
