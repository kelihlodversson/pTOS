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
#endif

static int target = 0x1234;
static int *ptr = &target;

int main(void)
{
#if defined(TEST_WITH_SHARED_VM_PEXEC)
    if(_base->p_cmdlin[0]==10 && _base->p_cmdlin[1]=='s') {
        uintptr_t address=0;unsigned i;struct shared_vm_probe *probe;
        for(i=3;i<11;i++) {
            unsigned char c=(unsigned char)_base->p_cmdlin[i];
            if(c>='0' && c<='9') c-='0';
            else if(c>='a' && c<='f') c=c-'a'+10;
            else Pterm(90);
            address=(address<<4)|c;
        }
        probe=(struct shared_vm_probe *)address;
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
