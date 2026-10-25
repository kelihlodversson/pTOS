/*
 * ldso_dynamic.c - regression test for the userspace runtime linker (#693)
 *
 * Launches a standard ELF executable (tests/ldso_runtime/program.c, linked at a
 * fixed address with a PT_INTERP) that needs two shared libraries.  The kernel
 * starts it through \GEMSYS\LIB\ARM\LD.SO, which loads the libraries
 * beside it, binds the program's data and functions, runs the preinit,
 * constructors and finalizers in their order and hands the program the finalizer;
 * the program checks all of it and exits with 77.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define EXPECTED 77L

#define EXIT_NO_LIBRARY 127L        /* what LD.SO exits with when it cannot link the program */

void test_ldso_dynamic(void);

#ifdef TEST_WITH_ELF_SHARED
static void meminfo(unsigned long *free_pages, unsigned long *processes)
{
    struct {
        unsigned long page_size, phys_pages, free_pages, processes;
        unsigned long shared_pages, shared_refs, shared_cross;
    } mi;

    *free_pages = *processes = 0;
    if (Ssystem(-3, (long)&mi, (long)sizeof(mi)) == (long)sizeof(mi))
    {
        *free_pages = mi.free_pages;
        *processes = mi.processes;
    }
}

/* programs that cannot start: a library that is not there, one that is cut short,
 * an interpreter that is not there.  Nothing may be left of them. */
static void failing_round(void)
{
    long rc;

    rc = Pexec(0, "C:\\NEEDMISS.ELF", "", 0L);
    ptest_assert_msg(rc == EXIT_NO_LIBRARY, "a program needing a missing library was not refused");
    rc = Pexec(0, "C:\\NEEDBAD.ELF", "", 0L);
    ptest_assert_msg(rc == EXIT_NO_LIBRARY, "a program needing a cut-short library was not refused");
    rc = Pexec(0, "C:\\NOINTRP.ELF", "", 0L);
    ptest_assert_msg(rc < 0, "a program with no interpreter was started");
}

static void test_failures(void)
{
    unsigned long free_before, free_after, procs_before, procs_after;
    int i;

    /* the first round may fill caches that stay (the directory, the file buffers) */
    failing_round();
    meminfo(&free_before, &procs_before);
    for (i = 0; i < 4; i++)
        failing_round();
    meminfo(&free_after, &procs_after);
    ptest_assert_msg(procs_after == procs_before, "a process record is left from a failed launch");
    ptest_assert_msg(free_after + 4 >= free_before, "memory is left from a failed launch");
}
#endif

void test_ldso_dynamic(void)
{
    ptest_begin("ldso_dynamic (#693)");
#ifdef TEST_WITH_ELF_SHARED
    {
        long rc = Pexec(0, "C:\\LDSOAPP.ELF", "", 0L);

        if (rc != EXPECTED)
        {
            ptest_fail(rc == 127 ? "the interpreter could not start the program"
                     : rc == 90 ? "the program got the wrong arguments"
                     : rc == 91 ? "the program saw the wrong initial state (constructors, data, calls)"
                     : rc == 92 ? "a finalizer did not run in order"
                     : rc == 93 ? "the program was given no finalizer"
                     : rc == 94 ? "the permissions of the program's memory are wrong"
                     : rc >= 0 && rc < 256 ? "the program reported another error"
                     : "the program did not run");
            return;
        }
        test_failures();
    }
#endif
    ptest_pass();
}
