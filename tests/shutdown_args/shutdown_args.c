/*
 * shutdown_args.c - regression test for Shutdown() (GEMDOS 0x151)
 *
 * Only the calls that must leave the system running can be tested here: a
 * mode that does not exist is refused with an error and nothing happens.
 * The real thing (the machine stops) is tests/shutdown_power/poweroff.c,
 * which tools/run-regression-tests.sh runs last and expects QEMU to leave.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define ERANGE  (-64L)

void test_shutdown_args(void);

void test_shutdown_args(void)
{
    long rc;

    ptest_begin("shutdown_args");

    rc = Shutdown(4L);
    ptest_assert_msg(rc == ERANGE, "Shutdown(4) did not refuse an unknown mode with ERANGE");
    rc = Shutdown(-1L);
    ptest_assert_msg(rc == ERANGE, "Shutdown(-1) did not refuse an unknown mode with ERANGE");

    /* still here, and files still work */
    rc = Fsfirst("C:\\*.*", 0);
    ptest_assert_msg(rc == 0, "the file system does not work after a refused Shutdown()");

    ptest_pass();
}
