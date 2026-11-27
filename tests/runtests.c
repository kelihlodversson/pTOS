/*
 * runtests.c - main test harness for pTOS regression tests
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "entry_args.h"
#include "test.h"
#include <mint/osbind.h>
#include <string.h>
#if defined(__arm__) && defined(TEST_NATIVE_ELF_ENTRY) && !defined(TEST_WITH_USER_ASPACE) && !defined(TEST_WITH_FAT_FILE_LIFETIMES)
int test_open_unlink_inherited(int foreign);
#endif
#if defined(TEST_WITH_MULTITHREADING) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
void pclone_run_managed_tests(void);
void pclone_run_unmanaged_tests(void);
void pclone_run_staged_tests(void);
long pclone_run_anchored_probe(void);
#if defined(TEST_WITH_IRQ_RESTART)
long pclone_run_irq_image(int role);
#endif
#endif

#if defined(TEST_WITH_SHARED_VM_PEXEC) && defined(TEST_WITH_MULTITHREADING)
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

int main(int argc, char **argv)
{
    const char *role = test_entry_argument(argc, argv);
    int failures;

    (void)role;
#if defined(__arm__) && defined(TEST_NATIVE_ELF_ENTRY) && !defined(TEST_WITH_USER_ASPACE) && !defined(TEST_WITH_FAT_FILE_LIFETIMES)
    if (!strcmp(role, "--inherit-fat"))
        return test_open_unlink_inherited(0);
    if (!strcmp(role, "--inherit-9p"))
        return test_open_unlink_inherited(1);
#endif
#if defined(TEST_WITH_SHARED_VM_PEXEC) && defined(TEST_WITH_MULTITHREADING)
    if (!strcmp(role, "--shared-vm")) {
        ptest_log_append();
        shared_vm_run_managed_tests();
        failures=ptest_failures();ptest_log_flush();Pterm(failures);return failures;
    }
#endif
#if defined(TEST_WITH_MULTITHREADING) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
    int worker = !strcmp(role, "--clone");
    int unmanaged = !strcmp(role, "--abi-unmanaged");
    int staged = !strcmp(role, "--abi-staged");
    if (worker || unmanaged || staged)
        ptest_log_append();
    /* #1015: run as a managed THREAD's Pexec(0) child, an anchor. */
    if (!strcmp(role, "--abi-anchored")) {
        long status = pclone_run_anchored_probe();
        Pterm((short)status);
        return (int)status;
    }
#if defined(TEST_WITH_IRQ_RESTART)
#ifdef TEST_NATIVE_ELF_ENTRY
    const char *irq_mode = !strcmp(role, "--irq") && argc == 3 ? argv[2] : "";
#else
    const char *irq_mode = !strncmp(role, "--irq ", 6) ? role + 6 : "";
#endif
    if (irq_mode[0] && !irq_mode[1] && strchr("ARNO", irq_mode[0])) {
        long status = pclone_run_irq_image(irq_mode[0]);
        Pterm((short)status);
        return (int)status;
    }
#endif
#endif

    conws("pTOS regression tests\r\n\r\n");

#if defined(TEST_WITH_MULTITHREADING) && (defined(__arm__) || defined(__mc68000__) || defined(__x86_64__))
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
