/* Public GEMDOS exercise used by the boot controller's ordinary Pexec(0)
 * and Pexec(100) calls, and by a managed parent's Pexec(0). */
#include "../entry_args.h"
#include <mint/basepage.h>
#include <mint/mintbind.h>
#include <mint/osbind.h>

#include "contract.h"
#include "../nested_exec/nested_query.h"

#if defined(__mc68000__) && defined(TEST_IS_PRG)
long _stksize = 16L * 1024;
#endif

#define WORKER_STACK 16384

#if defined(TEST_WITH_MULTITHREADING)
static volatile long worker_pid;
static volatile int worker_entered;

static void worker(void *argument)
{
    worker_pid = Pgetpid();
    worker_entered = 1;
    if (argument)
        *(volatile unsigned long *)0x80001000UL = 1; /* supervisor-only or unmapped */
    Pterm(47);
}

#if defined(TEST_WITH_MULTITHREADING)
static int exercise_clone(void)
{
    char *stack = (char *)Malloc(WORKER_STACK);
    struct ptos_clone_args args = {0};
    long self = Pgetpid();
    int32_t pid, tid, status, previous_tid = 0;
    int mode;

    if (!stack || (long)stack < 0 || self <= 0)
        return 1;
    args.entry = worker;
    args.stack_base = stack;
    args.stack_size = WORKER_STACK;
    args.child_pid = &pid;
    args.child_tid = &tid;
    for (mode = 0; mode < 4; mode++) {
        long result, completion;
        int sibling = mode & 1;
        int fault = mode & 2;
        int32_t expected = fault ? 0xffff : 47;

        args.flags = CLONE_VM | (sibling ? CLONE_SAME_PROCESS : 0);
        args.argument = fault ? stack : 0;
        pid = tid = worker_entered = worker_pid = 0;
        result = nested_trap2(GEMDOS_PCLONE, (long)&args, sizeof(args));
        if (pid <= 0 || tid <= 0 || tid == previous_tid ||
            result != tid || ((pid == self) != sibling))
            return 2 + mode;
        previous_tid = tid;
        if (sibling) {
            status = -1;
            if (nested_trap2(GEMDOS_PTHJOIN, tid, (long)&status) ||
                status != expected || nested_trap2(GEMDOS_PTHJOIN, tid, 0) >= 0)
                return 6 + mode;
        } else {
            completion = Pwaitpid((short)pid, 0, 0);
            /* Pwaitpid packs the signal into the high byte of the wait
             * status, while Pthjoin writes the full termination code. */
            if ((unsigned long)completion != ((unsigned long)pid << 16 | (fault ? 11U << 8 : 47U)) ||
                Pwaitpid((short)pid, 1, 0) != -33L)
                return 6 + mode;
        }
        if (!worker_entered || worker_pid != pid || Pgetpid() != self)
            return 10 + mode;
    }
    if (Mfree(stack) || Pwaitpid(-1, 1, 0) != -33L)
        return 14;
    return (int)FIRST_ANCHOR_STATUS;
}
#endif

static int check_unmanaged(void)
{
    char *stack = (char *)Malloc(WORKER_STACK);
    struct ptos_clone_args args = {0};

    if (!stack || (long)stack < 0)
        return 15;
    args.entry = worker;
    args.flags = CLONE_VM | CLONE_SAME_PROCESS;
    args.stack_base = stack;
    args.stack_size = WORKER_STACK;
    if (nested_trap2(GEMDOS_PCLONE, (long)&args, sizeof(args)) != NESTED_EINVFN ||
        worker_entered || Mfree(stack))
        return 16;
    return (int)FIRST_ANCHOR_STATUS;
}
#endif

int main(int argc, char **argv)
{
    const char *argument = test_entry_argument(argc, argv);

    if (argument[0] && !argument[1]) {
        switch (argument[0]) {
#if defined(TEST_WITH_MULTITHREADING)
        case 'm':
            return Pexec(0, FIRST_ANCHOR_PATH, "", 0);
#endif
        case 'u':
            return Pexec(0, FIRST_ANCHOR_PATH, "\1n", 0);
        case 'n':
#if defined(TEST_WITH_MULTITHREADING)
            return check_unmanaged();
#else
            /* The kernel verifies the untracked carrier and exit rollback. */
            return (int)FIRST_ANCHOR_STATUS;
#endif
        }
    }
#if defined(TEST_WITH_MULTITHREADING)
    return exercise_clone();
#else
    return 17;
#endif
}
