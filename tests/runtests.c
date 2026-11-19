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
void pclone_run_unmanaged_tests(void);
void pclone_run_staged_tests(void);
long pclone_run_anchored_probe(void);
#if defined(TEST_WITH_IRQ_RESTART)
long pclone_run_irq_image(int role);
#endif
#endif

#if defined(TEST_WITH_SHARED_VM_PEXEC) && defined(TEST_WITH_PCLONE) && defined(TEST_WITH_PTHJOIN)
void shared_vm_run_managed_tests(void);
#endif

#if defined(__mc68000__) && defined(TEST_IS_PRG)
/*
 * A classic PRG keeps its stack in its TPA, by default libcmini's MINKEEP
 * (64 KiB). On MMU m68k kernels the runner nests itself up to three deep
 * (top level, Pclone worker, nested ABI child) inside the 2 MiB procmem
 * window, where that default left the last copy no headroom at all: a few
 * hundred bytes of new test code made its startup fail with Pterm(-1).
 * The deepest runner frame is 16 KiB (tests/mmap), so 32 KiB still leaves
 * the call chain room. Private-stack builds (ELF, ARM, x32) are unaffected:
 * there a positive _stksize would instead become a Psetlimit() cap.
 */
long _stksize = 32L * 1024;
#endif

int main(void)
{
    int failures;
#if defined(TEST_WITH_SHARED_VM_PEXEC) && defined(TEST_WITH_PCLONE) && defined(TEST_WITH_PTHJOIN)
    if ((unsigned char)_base->p_cmdlin[0]==11 && !memcmp(_base->p_cmdlin+1,"--shared-vm",11)) {
        ptest_log_append();
        shared_vm_run_managed_tests();
        failures=ptest_failures();ptest_log_flush();Pterm(failures);return failures;
    }
#endif
#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
    int worker = (unsigned char)_base->p_cmdlin[0] == 7 &&
                 !memcmp(_base->p_cmdlin + 1, "--clone", 7);
    int unmanaged = (unsigned char)_base->p_cmdlin[0] == 15 &&
                    !memcmp(_base->p_cmdlin + 1,"--abi-unmanaged",15);
    int staged = (unsigned char)_base->p_cmdlin[0] == 12 &&
                 !memcmp(_base->p_cmdlin + 1,"--abi-staged",12);
    if (worker || unmanaged || staged)
        ptest_log_append();
    /* #1015: run as a managed THREAD's Pexec(0) child, an anchor. */
    if ((unsigned char)_base->p_cmdlin[0] == 14 && !memcmp(_base->p_cmdlin + 1, "--abi-anchored", 14)) {
        long status = pclone_run_anchored_probe();
        Pterm((short)status);
        return (int)status;
    }
#if defined(TEST_WITH_IRQ_RESTART)
    if ((unsigned char)_base->p_cmdlin[0]==7 && !memcmp(_base->p_cmdlin+1,"--irq ",6) &&
        _base->p_cmdlin[7] && strchr("ARNO",_base->p_cmdlin[7])) {
        long status=pclone_run_irq_image(_base->p_cmdlin[7]);
        Pterm((short)status);
        return (int)status;
    }
#endif
#endif

    conws("pTOS regression tests\r\n\r\n");

#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
    if (worker || unmanaged || staged) {
        if (unmanaged) pclone_run_unmanaged_tests();
        else if (staged) pclone_run_staged_tests();
        else pclone_run_managed_tests();
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
