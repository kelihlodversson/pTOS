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

void test_pie_load(void);

long test_start_pie_probe(void)
{
#if defined(__arm__) && defined(TEST_NATIVE_ELF_ENTRY) && !defined(TEST_WITH_USER_ASPACE)
    char *argv[] = { "custom name", "two words", "", NULL };
    char *envp[] = { "CHECK_LAYOUT=1", "", "AFTER_EMPTY=1", NULL };

    return Pexecve(0, "C:\\PIEPROBE.TOS", argv, envp);
#else
    return Pexec(PE_LOADGO, "C:\\PIEPROBE.TOS", "", 0);
#endif
}

#if defined(__arm__) && defined(TEST_NATIVE_ELF_ENTRY) && !defined(TEST_WITH_USER_ASPACE)
/* Exercise the same basepage sizing invariant from the running ELF child. */
static const char *check_footprint(void)
{
    static const long pads[] = { 0, 4, 8, 12, 16, 20, 24, 28 };
    void *held[sizeof pads / sizeof pads[0]];
    const char *failure = NULL;
    unsigned i;

    for (i = 0; i < sizeof pads / sizeof pads[0]; i++) {
        long before;

        held[i] = pads[i] ? (void *)Malloc(pads[i]) : NULL;
        before = Malloc(-1L);
        if (test_start_pie_probe() != 0)
            failure = "shared ARM ELF argv, environment or image footprint";
        if (Malloc(-1L) != before)
            failure = "shared ARM launch did not return its memory";
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

    rc = test_start_pie_probe();
    ptest_assert_msg(rc == 0,
                      "PIEPROBE.TOS exited nonzero "
                      "(PIE dynamic relocations not applied?)");

#if defined(TEST_WITH_LEGACY_PEXEC)
    /* #1018: 68k mode 3 stages a PRG only: this ELF is refused unloaded */
    ptest_assert_msg(Pexec(PE_LOAD, "C:\\PIEPROBE.TOS", "", 0) == -66L,
                     "mode 3 staged an ELF");
#elif defined(TEST_NATIVE_ELF_ENTRY)
    {
        static const short modes[] = { 3, 4, 5, 6, 7, 104, 106 };
        unsigned i;
        for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
            ptest_assert_msg(trap_1_wwlll(0x4b, modes[i], 1, 1, 1) == -32L,
                             "an unsupported staged Pexec mode was not EINVFN");
    }
#endif
#if defined(__arm__) && defined(TEST_NATIVE_ELF_ENTRY) && !defined(TEST_WITH_USER_ASPACE)
    {
        const char *failure = check_footprint();

        ptest_assert_msg(!failure, failure ? failure : "");
    }
#endif

    ptest_pass();
}
