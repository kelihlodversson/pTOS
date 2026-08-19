/*
 * stack_alignment.c - regression test for issue #214
 *
 * Verifies that the GEMDOS scratch stack (fstrt) is properly aligned.
 * A misaligned stack causes GCC-generated NEON loads/stores in makofd()
 * to fault with a Data Abort on ARM.
 *
 * The test calls Fsfirst() which exercises makofd() via the directory
 * open path.  If the stack is misaligned, this will trap.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

void test_stack_alignment(void)
{
    long rc;

    ptest_begin("stack_alignment (#214)");

    /*
     * Fsfirst() on a subdirectory exercises makofd(), whose struct
     * copies GCC may auto-vectorize with alignment-checked NEON
     * instructions (vld1.64 :64).  A misaligned fstrt stack causes
     * a Data Abort here.
     *
     * The root of C: works fine even with a misaligned stack (the
     * desktop opens C:\*.* at boot), but a subdirectory open is
     * what triggered the original crash.
     *
     * The test disk must contain a \TESTS directory for this to
     * exercise makofd(); otherwise Fsfirst returns ENOENT before
     * reaching the vulnerable code path.
     */
    rc = Fsfirst("C:\\TESTS\\*.*", 0);
    ptest_assert_msg(rc == -33 || rc == 0,
                     "Fsfirst on subdirectory returned unexpected error");

    ptest_pass();
}
