/* Pexec(200) acceptance subject (#970). Started by the Pclone runner as an
 * anchor PROCESS with Pexec(100), and again as a synchronously nested child
 * with Pexec(0) and the tail "N", which takes the launcher relaunch path.
 * Every injected or genuine preparation failure must leave this program
 * running unchanged, with every counter conserved; the genuine call replaces
 * it with the ELF image EXEC200B.TOS in the same PROCESS and never returns.
 * That image finally replaces itself with this program again (tail "F"):
 * on m68k, where this is a classic PRG, that is an ELF-to-PRG replacement.
 * The final program completes the PROCESS with status 70. */
#include "exec200_check.h"
#include "../../include/mapping_lifetime_test.h"
#include <mint/mintbind.h>

static EXEC_REPLACE_TEST_STATE original;
static long descriptor;
static char environment[] = "EXEC=970\0SECOND=yes\0";
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static MAPPING_LIFETIME_SNAPSHOT before, after;
static int baseline; /* set once the warmed-up counters are recorded */
static void snapshot(MAPPING_LIFETIME_SNAPSHOT *out)
{
    memset(out, 0, sizeof(*out));
    if (exec200_probe(out, MLT_SNAPSHOT)) Pterm(81);
}
#endif
static void fail(const char *reason, long value)
{
    printf("Pexec200 subject FAIL: %s (%ld)\r\n", reason, value);
    Pterm(83);
}
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void conserved(const MAPPING_LIFETIME_SNAPSHOT *reference, const char *what);
#endif
static void unchanged(const char *what)
{
    EXEC_REPLACE_TEST_STATE now;
    if (exec200_probe(&now, EXEC_REPLACE_QUERY)) fail("query", 0);
    if (memcmp(&now, &original, sizeof(now))) fail(what, (long)now.generation[0]);
    if (Pgetpid() != (long)original.pid) fail("PID after failure", Pgetpid());
    if (Fwrite((short)descriptor, 1, "a") != 1) fail("descriptor after failure", descriptor);
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (!baseline) return;
    conserved(&before, what);
#endif
}
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void conserved(const MAPPING_LIFETIME_SNAPSHOT *reference, const char *what)
{
    unsigned i;
    snapshot(&after);
    for (i = 0; i < MLT_FIELDS; i++) if (reference->value[i] != after.value[i]) {
        /* Retired startup state can take or return one slab page. */
        if (i == MLT_KHEAP_PAGES &&
            (reference->value[i] - after.value[i] == 1 || after.value[i] - reference->value[i] == 1))
            continue;
        printf("Pexec200 counter %u: %lu -> %lu after %s\r\n", i,
               (unsigned long)reference->value[i], (unsigned long)after.value[i], what);
        Pterm(82);
    }
}
#endif
static void injected_target(const char *path, unsigned kind, unsigned stage, long expected)
{
    long result;
    char message[48];
    exec200_probe(0, EXEC_REPLACE_ARM + (kind << 8) + stage);
    result = exec200_call(path, "\0", environment);
    exec200_probe(0, EXEC_REPLACE_ARM);
    sprintf(message, "injected failure %u.%u %s", kind, stage, path);
    if (result != expected) fail(message, result);
    unchanged(message);
}
static void injected(unsigned kind, unsigned stage, long expected)
{
    injected_target("EXEC200B.TOS", kind, stage, expected);
}
/* The last program of the chain, after an ELF image replaced itself with
 * this one again. */
static void final_program(void)
{
    EXEC200_TAIL t;
    exec200_verify("final", &t);
    if (t.generation != 3) exec200_fail("final", "generation", (long)t.generation);
    if (Fsnext() == 0) exec200_fail("final", "search survived the exec", 0);
    if (Fclose((short)t.descriptor)) exec200_fail("final", "descriptor close", t.descriptor);
    Cconws(t.nested == 2 ? "native Pexec(200) from a private-VM clone: PASS\r\n" :
           t.nested ? "native nested Pexec(200) launcher relaunch: PASS\r\n"
                    : "native Pexec(200) same-PROCESS executable/root replacement: PASS\r\n");
    Pterm(70);
}
/* Every injected preparation failure, then the genuine replacement: the
 * same sequence for the subject and for its private-VM clone. */
static void failures(void)
{
    unsigned stage;
    long result;
    if ((result = exec200_call("NO970.TOS", "\0", environment)) != -33L) fail("missing file", result);
    unchanged("missing file");
    if ((result = exec200_call("LOADFAIL.TOS", "\0", environment)) >= 0) fail("load failure", result);
    unchanged("load failure");
    for (stage = 1; stage <= 4; stage++) injected(3, stage, -39L);
    for (stage = 1; stage <= 8; stage++) injected(1, stage, -39L);
#if defined(__mc68000__)
    /* The same bundle stages with a classic PRG target. */
    for (stage = 1; stage <= 8; stage++) injected_target("EXEC200.TOS", 1, stage, -39L);
#endif
    for (stage = 1; stage <= 10; stage++)
        injected(2, stage, stage == 6 || stage == 7 ? -36L : stage == 9 ? -326L : -39L);
}
static void replace(int nested)
{
    EXEC200_TAIL t;
    long result;
    /* The genuine commit: same PROCESS, different executable, basepage and
     * root. The tail starts the chain at generation 1. */
    memset(&t, 0, sizeof(t));
    t.generation = 0; t.descriptor = descriptor;
    t.pid = original.pid; t.tid = original.tid;
    t.root0 = original.root[0]; t.root1 = original.root[1];
    t.parent = Pgetppid(); t.nested = nested;
    result = exec200_next("EXEC200B.TOS", 0, &t, environment);
    fail("successful Pexec(200) returned to the old program", result);
}
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
/* #1003: a private-VM (COW) clone, what Pfork/Pvfork create, replaces
 * itself in its own PROCESS while its parent waits. */
static volatile unsigned long clone_cell = 0x5eed1003UL;
static int clone_exec;
static void clone_entry(void *argument)
{
    (void)argument;
    clone_cell = 0xc10e1003UL;   /* a private write the parent must never see */
    if (!clone_exec) Pterm(70);  /* the control: the same clone, no exec */
    baseline = 0;
    if (exec200_probe(&original, EXEC_REPLACE_QUERY)) fail("clone query", 0);
    if (original.generation[0] || original.generation[1]) fail("clone generation", 1);
    if (original.pid != (unsigned long)Pgetpid()) fail("clone PID", Pgetpid());
    descriptor = Fcreate("EXEC200C.TMP", 0);
    if (descriptor < 0) fail("clone descriptor", descriptor);
    injected(2, 10, -39L);
    snapshot(&before);
    baseline = 1;
    failures();
    replace(2);
}
static void clone_run(int exec)
{
    struct ptos_clone_args args;
    unsigned char *stack = (unsigned char *)Malloc(32768);
    long child, status;
    clone_exec = exec;
    if (!stack) fail("clone stack", 0);
    memset(stack, 0, 32768); memset(&args, 0, sizeof(args));
    args.flags = CLONE_VM | CLONE_COPY_STANDARD_HANDLES | CLONE_COPY_CWD; /* private-VM test selector */
    args.entry = clone_entry; args.stack_base = stack; args.stack_size = 32768;
    child = exec200_probe(&args, 0xc8920000UL | sizeof(args));
    if (child <= 0 || child == Pgetpid()) fail("private clone", child);
    status = Pwaitpid(child, 0, 0);
    if ((status >> 16) != child || (status & 65535) != 70) fail("clone Pexec(200) chain", status);
    if (Pwaitpid(child, 1, 0) >= 0) fail("clone reaped once", child);
    if (clone_cell != 0x5eed1003UL) fail("parent pages after the clone's exec", (long)clone_cell);
    if (Mfree(stack)) fail("clone stack release", 0);
    if (exec && Fdelete("EXEC200C.TMP")) fail("clone descriptor closed", 0);
}
/* Cloning leaves this program's own pages copy-on-write tracked, so the
 * reference is a control clone that exits without replacing itself: the
 * replacement must leave exactly what that one does. */
static void clone_case(void)
{
    MAPPING_LIFETIME_SNAPSHOT control;
    clone_run(0);
    clone_run(0);
    snapshot(&control);
    clone_run(1);
    conserved(&control, "private clone Pexec(200)");
    baseline = 0;               /* this program's own state, not its counters */
    unchanged("private clone Pexec(200)");
    baseline = 1;
    snapshot(&before);
}
#endif
int main(void)
{
    int nested = (unsigned char)_base->p_cmdlin[0] >= 1 && _base->p_cmdlin[1] == 'N';
    if ((unsigned char)_base->p_cmdlin[0] >= 1 && _base->p_cmdlin[1] == 'F') final_program();
    if (exec200_probe(&original, EXEC_REPLACE_QUERY)) fail("initial query", 0);
    if (original.generation[0] || original.generation[1]) fail("initial generation", 1);
    if (original.pid != (unsigned long)Pgetpid()) fail("initial PID", Pgetpid());
    descriptor = Fcreate("EXEC200.TMP", 0);
    if (descriptor < 0) fail("descriptor open", descriptor);
    /* Warm every allocation cache a successful preparation touches, then
     * take the baseline every later failure is compared against. */
    injected(2, 10, -39L);
#if defined(__mc68000__)
    injected_target("EXEC200.TOS", 2, 10, -39L);
#endif
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (!nested) {
        /* The clone's stack is this program's first Malloc block: grow the
         * heap ledger to its kept capacity before the baseline. */
        void *warm = (void *)Malloc(32768);
        if (!warm || Mfree(warm)) fail("heap warm-up", 0);
    }
    snapshot(&before);
    baseline = 1;
#endif
    failures();
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (!nested) clone_case();
#endif
    replace(nested);
    return 84;
}
