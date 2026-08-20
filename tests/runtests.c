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

int main(void)
{
    int failures;

    conws("pTOS regression tests\r\n\r\n");

    ptest_run_tests();

    failures = ptest_summary();
    if (failures)
        conws("\r\nSome tests FAILED.\r\n");
    else
        conws("\r\nAll tests passed.\r\n");

    Pterm(failures);
    return failures;
}
