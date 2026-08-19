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
     * The test disk (test-hd.img, via TEST_DESTDIR in the top-level
     * Makefile) always ships a \TESTS directory so this reaches
     * makofd(); tolerating ENOENT (-33) here would make the test pass
     * without ever exercising the vulnerable code path, silently
     * masking a broken disk-image packaging as well as the original
     * bug.
     */
    rc = Fsfirst("C:\\TESTS\\*.*", 0);
    ptest_assert_msg(rc == 0,
                     "Fsfirst on subdirectory returned unexpected error "
                     "(check that C:\\TESTS exists on the test disk)");

    ptest_pass();
}
