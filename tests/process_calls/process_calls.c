/*
 * process_calls.c - regression test for the MiNT process calls
 *
 * Process ids, the user, group and audit ids, the priority, Prusage() and
 * Dgetcwd(), where processes have a kernel record (CONF_WITH_KPROC: the
 * x86-64 port).  Elsewhere the calls are not there (EINVFN) and the suite
 * passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <string.h>

#define EINVFN  (-32L)
#define EINVAL  (-25L)
#define ESRCH   (-20L)
#define EIMBA   (-40L)
#define ERANGE  (-64L)

void test_process_calls(void);

void test_process_calls(void)
{
    long pid, ppid;
    long usage[8];
    char cwd[128], path[128];
    int i;

    ptest_begin("process_calls");

    pid = Pgetpid();
    if (pid == EINVFN)
    {
#ifdef __x86_64__
        ptest_fail("the process calls are missing");
#else
        ptest_pass();               /* no process records here */
#endif
        return;
    }

    /* the process ids */
    ptest_assert_msg(pid >= 1 && pid <= 32767, "Pgetpid() is not a process id");
    ppid = Pgetppid();
    ptest_assert_msg(ppid >= 0 && ppid <= 32767 && ppid != pid, "Pgetppid() is not another process");
    ptest_assert_msg(Pgetpid() == pid, "Pgetpid() changed");

    /* there are no users: every id is root's 0 */
    ptest_assert_msg(Pgetuid() == 0 && Pgeteuid() == 0, "the user id is not 0");
    ptest_assert_msg(Pgetgid() == 0 && Pgetegid() == 0, "the group id is not 0");
    ptest_assert_msg(Pgetauid() == 0, "the audit id is not 0");
    ptest_assert_msg(Psetauid(0) == 0, "Psetauid(0) failed");
    ptest_assert_msg(Psetauid(5) == EINVAL, "Psetauid(5) was not refused");

    /* the priority: kept, and read back as FreeMiNT gives it, 20 minus it */
    ptest_assert_msg(Pgetpriority(0, 0) == 20, "the priority does not start at 0");
    ptest_assert_msg(Pgetpriority(0, pid) == 20, "Pgetpriority by the own pid differs");
    ptest_assert_msg(Psetpriority(0, 0, 5) == 0, "Psetpriority(5) failed");
    ptest_assert_msg(Pgetpriority(0, 0) == 15, "the priority was not kept");
    ptest_assert_msg(Psetpriority(0, 0, 100) == 0 && Pgetpriority(0, 0) == 0, "the priority is not clamped to 20");
    ptest_assert_msg(Psetpriority(0, 0, -100) == 0 && Pgetpriority(0, 0) == 40, "the priority is not clamped to -20");
    ptest_assert_msg(Psetpriority(0, 0, 0) == 0 && Pgetpriority(0, 0) == 20, "the priority cannot be put back");
    ptest_assert_msg(Pgetpriority(1, 0) == 20 && Pgetpriority(2, 0) == 20, "group and user priority");
    ptest_assert_msg(Pgetpriority(0, (int)pid + 1) == ESRCH, "another process has a priority");
    ptest_assert_msg(Pgetpriority(2, 1) == ESRCH, "another user has a priority");
    ptest_assert_msg(Pgetpriority(0, -1) == EINVAL, "a negative process id was not refused");
    ptest_assert_msg(Pgetpriority(7, 0) == EINVAL, "a priority class that does not exist was not refused");

    /* Prusage(): times in ms, the memory the process has, the rest 0 */
    for (i = 0; i < 8; i++)
        usage[i] = -1;
    ptest_assert_msg(Prusage(usage) == 0, "Prusage() failed");
    ptest_assert_msg(usage[1] >= 0, "Prusage(): the user time");
#ifdef __x86_64__
    /* the pages of its address space; a process without one has none to count */
    ptest_assert_msg(usage[4] > 0, "Prusage(): the memory");
#endif
    ptest_assert_msg(usage[0] == 0 && usage[5] == 0 && usage[6] == 0 && usage[7] == 0,
                     "Prusage(): what is not kept is not 0");
#ifdef __x86_64__
    /* (pointers are checked where processes have address spaces) */
    ptest_assert_msg(Prusage((long *)0x100) == EIMBA, "Prusage() into an unmapped address was not refused");
#endif

    /* Dgetcwd(): what Dgetpath() says, with a size */
    memset(cwd, 'x', sizeof(cwd));
    memset(path, 'y', sizeof(path));
    ptest_assert_msg(Dgetpath(path, 0) == 0, "Dgetpath() failed");
    ptest_assert_msg(Dgetcwd(cwd, 0, (int)sizeof(cwd)) == 0, "Dgetcwd() failed");
    ptest_assert_msg(strcmp(cwd, path) == 0, "Dgetcwd() differs from Dgetpath()");
    ptest_assert_msg(Dgetcwd(cwd, 0, 0) == ERANGE, "Dgetcwd() with no room was not refused");
    ptest_assert_msg(Dgetcwd(cwd, 0, (int)strlen(path)) == ERANGE, "Dgetcwd() one byte short was not refused");
    ptest_assert_msg(Dgetcwd(cwd, 0, (int)strlen(path) + 1) == 0, "Dgetcwd() with exactly the room failed");
#ifdef __x86_64__
    ptest_assert_msg(Dgetcwd((char *)0x100, 0, 64) == EIMBA, "Dgetcwd() into an unmapped address was not refused");
#endif

    /* what cannot be honoured is refused: nothing to lock out, no scheduler to nice,
     * no permission bits to mask */
    ptest_assert_msg(Dlock(1, 2) == EINVFN, "Dlock() was not refused");
    ptest_assert_msg(Pnice(1) == EINVFN, "Pnice() was not refused");
    ptest_assert_msg(Pumask(022) == EINVFN, "Pumask() was not refused");

#ifdef __x86_64__
    /* Mvalidate() takes the own pid now (it is there with address spaces) */
    ptest_assert_msg(Mvalidate((int)pid, cwd, 16L, 0) == 0, "Mvalidate() with the own pid failed");
#endif

    ptest_pass();
}
