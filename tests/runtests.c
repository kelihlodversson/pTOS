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

extern void ptest_run_tests(void);

int main(void)
{
    conws("pTOS regression tests\n\n");

    ptest_run_tests();

    if (ptest_summary())
        conws("\nSome tests FAILED.\n");
    else
        conws("\nAll tests passed.\n");

    Pterm(0);
    return 0;
}
