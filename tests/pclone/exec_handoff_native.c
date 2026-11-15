/* Same-image transport, deliberately NOT Pexec(200) acceptance. The parent
 * waits for this ordinary managed PROCESS's one independent final completion. */
#include "../../include/pclone.h"
#include "../../include/exec_handoff_test.h"
#include "../../include/mapping_lifetime_test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <stdio.h>
#include <string.h>

static long probe(const void *args, unsigned long tag)
{
#if defined(__mc68000__)
    register long result __asm__("d0");
    __asm__ volatile (
        "move.l %2,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t"
        "move.w #0x190,-(%%sp)\n\ttrap #1\n\tlea 10(%%sp),%%sp"
        : "=d"(result) : "r"(args), "r"(tag)
        : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
    return result;
#elif defined(__arm__)
    register long r0 __asm__("r0") = GEMDOS_PCLONE;
    register const void *r1 __asm__("r1") = args;
    register unsigned long r2 __asm__("r2") = tag;
    __asm__ volatile ("svc 1" : "+r"(r0) : "r"(r1), "r"(r2)
                      : "r3", "r12", "lr", "memory", "cc");
    return r0;
#else
    return trap_1_wll(GEMDOS_PCLONE, (long)args, (long)tag);
#endif
}
static struct ptos_clone_args request;
static EXEC_HANDOFF_TEST_STATE original, previous;
static long stacks[2], descriptor, parent;
static unsigned cycle;
static volatile int old_return;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static MAPPING_LIFETIME_SNAPSHOT before, after;
static void snapshot(MAPPING_LIFETIME_SNAPSHOT *out)
{
    memset(out, 0, sizeof(*out));
    if (probe(out, MLT_SNAPSHOT)) Pterm(91);
}
static void conserve(void)
{
    unsigned i;
    snapshot(&after);
    for (i = 0; i < MLT_FIELDS; i++) if (before.value[i] != after.value[i]) {
        printf("exec transport counter %u: %lu -> %lu cycle %u\r\n", i,
            (unsigned long)before.value[i], (unsigned long)after.value[i], cycle);
        Pterm(92);
    }
}
#endif
static void fail(const char *reason)
{
    printf("exec transport FAIL cycle %u: %s\r\n", cycle, reason);
    Pterm(93);
}
static void next(void *argument);
static void transfer(void)
{
    unsigned stage;
    request.entry = next;
    request.argument = (void *)0x970UL;
    request.stack_base = (void *)stacks[cycle & 1]; request.stack_size = 32768;
    /* Warm bridge/allocation caches and a durable stack hold with rollback.
     * Every later failure and success compares the unchanged 45 counters. */
    if (probe(&request, EXEC_HANDOFF_RUN + 10) != -39L) fail("sealed rollback");
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (cycle >= 2) snapshot(&before);
#endif
    for (stage = 1; stage <= EXEC_HANDOFF_FAILURES; stage++) {
        long result = probe(&request, EXEC_HANDOFF_RUN + stage);
        if (result != (stage == 11 ? -326L : (stage == 7 || stage == 8) ? -36L : -39L))
            fail("failure prefix");
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
        if (cycle >= 2) conserve();
#endif
    }
    if (probe(&previous, EXEC_HANDOFF_QUERY)) fail("source identity query");
    (void)probe(&request, EXEC_HANDOFF_RUN);
    old_return = 1;
    fail("old syscall returned");
}
static void next(void *argument)
{
    EXEC_HANDOFF_TEST_STATE state;
    volatile unsigned stack_marker = 0x970;
    unsigned long sp = (unsigned long)&stack_marker;
    if (argument != (void *)0x970UL || old_return ||
        sp < (unsigned long)stacks[cycle & 1] ||
        sp >= (unsigned long)stacks[cycle & 1] + 32768) fail("fresh user startup/stack");
    if (probe(&state, EXEC_HANDOFF_QUERY) || state.pid != original.pid ||
        state.tid != original.tid || memcmp(state.process, original.process, sizeof(state.process)) ||
        memcmp(state.root, original.root, sizeof(state.root)) ||
        !memcmp(state.execution, previous.execution, sizeof(state.execution)) ||
        state.handoffs != cycle + 1 || Pgetpid() != (long)original.pid ||
        Pgetppid() != parent) fail("PROCESS/PID/TID/root/execution identity");
    if (Fwrite((short)descriptor, 1, "x") != 1) fail("descriptor continuity");
    if (Mfree((void *)stacks[cycle & 1]) >= 0) fail("active user stack lost its hold");
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (cycle >= 2) conserve();
#endif
    cycle++;
    if (cycle < 12) transfer();
    if (Fclose((short)descriptor) || Fdelete("HANDOFF.TMP")) fail("descriptor close");
    if (Mfree((void *)stacks[cycle & 1])) fail("retired user stack still held");
    Cconws("native same-PROCESS/TID NORETURN fresh-stack transport (not Pexec200): PASS\r\n");
    Pterm(0);
}
int main(void)
{
    if (probe(&original, EXEC_HANDOFF_QUERY) || original.handoffs) fail("initial managed identity");
    parent = Pgetppid();
    stacks[0] = Malloc(32768); stacks[1] = Malloc(32768);
    if (stacks[0] <= 0 || stacks[1] <= 0) fail("user stack allocation");
    memset((void *)stacks[0], 0, 32768); memset((void *)stacks[1], 0, 32768);
    descriptor = Fcreate("HANDOFF.TMP", 0);
    if (descriptor < 0) fail("descriptor open");
    transfer();
    return 94;
}
