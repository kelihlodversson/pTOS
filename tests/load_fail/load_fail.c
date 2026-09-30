/*
 * load_fail.c - regression test for xexec() load failure cleanup
 *
 * LOADFAIL.TOS has a valid ELF header but no loadable segment.  Pexec()
 * therefore creates a basepage and KPROC before kpgmld() fails.  Repeating
 * the failure must not consume KPROCs; a final normal launch confirms that
 * the fixed kernel pool remains usable.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

#define LOAD_FAILURES 128

void test_load_fail(void);

void test_load_fail(void)
{
    int i;
    long rc;

    ptest_begin("load_fail");

    for (i = 0; i < LOAD_FAILURES; i++) {
        rc = Pexec(PE_LOAD, "C:\\LOADFAIL.TOS", "", 0);
        ptest_assert_msg(rc < 0, "truncated ELF unexpectedly loaded");
    }

    rc = Pexec(PE_LOADGO, "C:\\PIEPROBE.TOS", "", 0);
    ptest_assert_msg(rc == 0,
                     "failed loads exhausted KPROC sidecars");

    ptest_pass();
}
