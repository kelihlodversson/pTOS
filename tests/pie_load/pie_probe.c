/*
 * pie_probe.c - PIE payload for the pie_load regression test
 *
 * A standalone executable, not a ptest_* suite -- it is launched by
 * pie_load.c's test_pie_load() via Pexec(), not linked into runtests.tos.
 * It must be built and linked as a position-independent ET_DYN
 * ("ld -pie --no-dynamic-linker") so that loading it exercises
 * elf_pgmld()'s handling of the .rel.dyn / .rela.dyn dynamic relocations
 * a PIE carries (see bdos/elfld.c). A statically initialized pointer to
 * another global forces the linker to emit one: without any absolute
 * pointer in initialized data, a PIE needs no relocations at all and the
 * loader path under test would never run.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#if defined(TEST_WITH_SHARED_VM_PEXEC)
#include <mint/osbind.h>
#include <mint/basepage.h>
#include <mint/mintbind.h>
#include "shared_vm_probe.h"
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#endif

#if defined(TEST_WITH_SHARED_VM_PEXEC)
static void probe_tail(char *tail,char role,struct shared_vm_probe *probe)
{
    uintptr_t address=(uintptr_t)probe;unsigned i;
    tail[0]=10;tail[1]=role;tail[2]=' ';
    for(i=0;i<8;i++) tail[3+i]="0123456789abcdef"[(address>>(28-4*i))&15];
    tail[11]=0;
}
static long probe_thread(void (*entry)(void *),struct shared_vm_probe *probe,void *stack)
{
    struct ptos_clone_args args={0};
    args.flags=CLONE_VM|CLONE_SAME_PROCESS;args.entry=entry;args.argument=probe;
    args.stack_base=stack;args.stack_size=16384;
    return trap_1_wll(GEMDOS_PCLONE,(long)&args,sizeof(args));
}
static void nested_worker(void *argument)
{
    struct shared_vm_probe *probe=argument;
    if((uint32_t)Pgetpid()!=probe->pid) probe->error=21;
    probe->steps++;
}
static void surviving_worker(void *argument)
{
    struct shared_vm_probe *probe=argument;unsigned spin;void *stack;long tid;int32_t status=-1;
    if((uint32_t)Pgetpid()!=probe->pid) probe->error=22;
    probe->worker_started=1;
    for(spin=0;spin<512 && !probe->gate;spin++) (void)Syield();
    if(!probe->gate) {probe->error=23;Pterm(94);}
    stack=(void *)Malloc(16384);
    if((long)stack<=0) {probe->error=24;Pterm(94);}
    tid=probe_thread(nested_worker,probe,stack);probe->second_tid=(uint32_t)tid;
    if(tid<=0 || trap_1_wll(GEMDOS_PTHJOIN,tid,(long)&status) || status) probe->error=25;
    if(Mfree(stack)) probe->error=26;
    probe->steps++;probe->done=1;Pterm(0x42);
}
#endif

static int target = 0x1234;
static int *ptr = &target;

int main(void)
{
#if defined(TEST_WITH_SHARED_VM_PEXEC)
    if(_base->p_cmdlin[0]==10 && (_base->p_cmdlin[1]=='s' || _base->p_cmdlin[1]=='o' ||
        _base->p_cmdlin[1]=='b' || _base->p_cmdlin[1]=='l' || _base->p_cmdlin[1]=='p' || _base->p_cmdlin[1]=='g')) {
        uintptr_t address=0;unsigned i;struct shared_vm_probe *probe;
        for(i=3;i<11;i++) {
            unsigned char c=(unsigned char)_base->p_cmdlin[i];
            if(c>='0' && c<='9') c-='0';
            else if(c>='a' && c<='f') c=c-'a'+10;
            else Pterm(90);
            address=(address<<4)|c;
        }
        probe=(struct shared_vm_probe *)address;
        if(_base->p_cmdlin[1]=='p') {
            probe->core_limit=(uint32_t)Psetlimit(2,-1);
            probe->data_limit=(uint32_t)Psetlimit(3,-1);
            probe->stack_limit=(uint32_t)Psetlimit(0x100,-1);
            probe->same_limit=(uint32_t)Psetlimit(0x100,probe->stack_limit);
            probe->resize_limit=(uint32_t)Psetlimit(0x100,probe->stack_limit+4096);
            probe->started=1;
            /* Outside the inherited 1MiB top-stack reservation and the
             * child's actual guarded heap stack. No logical owner exists. */
            *(volatile uint32_t *)(uintptr_t)0x7f001000UL=0x894;
            probe->done=1;Pterm(0x55);
        }
        if(_base->p_cmdlin[1]=='l') {
            void *stack=(void *)Malloc(16384);long tid;
            probe->pid=(uint32_t)Pgetpid();probe->worker_stack=(uint32_t)(uintptr_t)stack;
            if((long)stack<=0) {probe->error=27;Pterm(94);}
            tid=probe_thread(surviving_worker,probe,stack);probe->worker_tid=(uint32_t)tid;
            if(tid<=0) {probe->error=28;(void)Mfree(stack);probe->worker_stack=0;Pterm(94);}
            Pterm(0x41);
        }
        if(_base->p_cmdlin[1]=='b') {
            unsigned spin;BASEPAGE *grandchild;
            probe->started=1;
            for(spin=0;spin<512 && !probe->gate;spin++) (void)Syield();
            if(!probe->gate) {probe->error=11;Pterm(94);}
            grandchild=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS","",0);
            if((long)grandchild<=0) {probe->error=12;Pterm(94);}
            probe->grandchild=(uint32_t)(uintptr_t)grandchild;
            probe->grand_environment=(uint32_t)(uintptr_t)grandchild->p_env;
            probe->done=1;Pterm(0x33);
        }
        if(_base->p_cmdlin[1]=='o' || _base->p_cmdlin[1]=='g') {
            char tail[12];BASEPAGE *staged;
            if(_base->p_cmdlin[1]=='g') {
                unsigned spin;void *stack=(void *)Malloc(16384);long tid;
                probe->pid=(uint32_t)Pgetpid();probe->worker_stack=(uint32_t)(uintptr_t)stack;
                if((long)stack<=0) {probe->error=41;Pterm(94);}
                tid=probe_thread(surviving_worker,probe,stack);probe->worker_tid=(uint32_t)tid;
                if(tid<=0) {probe->error=42;(void)Mfree(stack);probe->worker_stack=0;Pterm(94);}
                for(spin=0;spin<512 && !probe->worker_started;spin++) (void)Syield();
                if(!probe->worker_started) {probe->error=43;Pterm(94);}
            }
            probe_tail(tail,'b',probe);staged=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS",tail,0);
            if((long)staged<=0) {probe->error=13;Pterm(94);}
            probe->staged=(uint32_t)(uintptr_t)staged;
            probe->environment=(uint32_t)(uintptr_t)staged->p_env;
            {
                long pid=Pexec(104,0,staged,0);
                if(pid<=0) {probe->error=14;Pterm(94);}
                probe->child_pid=(uint32_t)pid;
            }
            if(_base->p_cmdlin[1]=='g') {
                (void)trap_1_ww(GEMDOS_PTERMGRP,0x63);
                probe->error=44;Pterm(94);
            }
            Pterm(0x32);
        }
        {
            BASEPAGE *staged=(BASEPAGE *)Pexec(3,"C:\\PIEPROBE.TOS","",0);
            if((long)staged<=0) {probe->error=(uint32_t)(long)staged;Pterm(91);}
            probe->staged=(uint32_t)(uintptr_t)staged;
            probe->environment=(uint32_t)(uintptr_t)staged->p_env;
            probe->pid=(uint32_t)Pgetpid();probe->done=1;
        }
        Pterm(0x31);
    }
    if(_base->p_cmdlin[0]==1 && _base->p_cmdlin[1]=='f') Pterm(0xffff);
    if(_base->p_cmdlin[0]==1 && _base->p_cmdlin[1]=='i') {
#if defined(__x86_64__)
        __asm__ volatile("ud2");
#elif defined(__arm__)
        /* UDF encodings are pTOS's emulated Line-A namespace. Use a
         * genuinely undefined ARM instruction outside that reserved pattern. */
        __asm__ volatile(".word 0xe6000010");
#elif defined(__mc68000__)
        __asm__ volatile("illegal");
#endif
        Pterm(91);
    }
#endif
    return (*ptr == 0x1234) ? 0 : 1;
}
