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
#if defined(TEST_WITH_ELF_SHARED)
    shared_vm_interpreted();
#endif
#if defined(__mc68000__)
    shared_vm_prg();
#endif
#endif
    ptest_pass();
}
