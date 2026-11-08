/*
 * runtests.c - main test harness for pTOS regression tests
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <string.h>
#include <mint/basepage.h>
#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
void pclone_run_managed_tests(void);
#endif

int main(void)
{
    int failures;
#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
    int worker = (unsigned char)_base->p_cmdlin[0] == 7 &&
                 !memcmp(_base->p_cmdlin + 1, "--clone", 7);
    if (worker)
        ptest_log_append();
#endif

    conws("pTOS regression tests\r\n\r\n");

#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
    if (worker) {
        pclone_run_managed_tests();
        failures = ptest_failures();
        ptest_log_flush();
        /* Only the outer driver may emit the suite summary/verdict. A child
         * PASS must not conceal a later fault in the remaining parent tests. */
        Pterm(failures);
        return failures;
    }
#endif
    ptest_run_tests();

    failures = ptest_summary();
    if (failures)
        conws("\r\nSome tests FAILED.\r\n");
    else
        conws("\r\nAll tests passed.\r\n");
    ptest_log_flush();

    Pterm(failures);
    return failures;
}
