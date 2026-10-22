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
#include <mint/basepage.h>
#include <stddef.h>
#include <stdint.h>

void test_pie_load(void);

#if defined(__arm__) || defined(__mc68000__)

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

#if defined(__arm__) || defined(__mc68000__)
    {
        const char *failure = check_footprint();

        ptest_assert_msg(!failure, failure ? failure : "");
    }
#endif

    ptest_pass();
}
