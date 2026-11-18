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
/* m68k test kernels only (#1018): a staged PRG, started with 4/104 as a
 * distinct PROCESS sharing the caller's address space. */
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

void test_pie_load(void);

void test_pie_load(void)
{
    long rc;

    ptest_begin("pie_load");

    rc = Pexec(PE_LOADGO, "C:\\PIEPROBE.TOS", "", 0);
    ptest_assert_msg(rc == 0,
                      "PIEPROBE.TOS exited nonzero "
                      "(PIE dynamic relocations not applied?)");

#if defined(TEST_WITH_LEGACY_PEXEC)
    /* #1018: 68k mode 3 stages a PRG only: this ELF is refused unloaded */
    ptest_assert_msg(Pexec(PE_LOAD, "C:\\PIEPROBE.TOS", "", 0) == -66L,
                     "mode 3 staged an ELF");
#else
    {
        /* #1018: ARM/x32 have no staged modes; refused before any argument
         * is used, so the bogus pointers below are never looked at */
        static const short modes[] = { 3, 4, 5, 6, 7, 104, 106 };
        unsigned i;
        for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
            ptest_assert_msg(Pexec(modes[i], (void *)1, (void *)1, (void *)1) == -32L,
                             "an unsupported staged Pexec mode was not EINVFN");
    }
#endif

#if defined(TEST_WITH_SHARED_VM_PEXEC)
    shared_vm_prg();
#endif
    ptest_pass();
}
