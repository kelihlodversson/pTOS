/* Pexec(200) acceptance subject (#970). Started by the Pclone runner as an
 * anchor PROCESS with Pexec(100), and again as a synchronously nested child
 * with Pexec(0) and the tail "N", which takes the launcher relaunch path. Every injected or genuine preparation
 * failure must leave this program running unchanged, with every counter
 * conserved; the final call replaces it with EXEC200B.TOS in the same
 * PROCESS and never returns. */
#include "exec200_probe.h"
#include "../../include/mapping_lifetime_test.h"
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include <stdio.h>
#include <string.h>

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
static void unchanged(const char *what)
{
    EXEC_REPLACE_TEST_STATE now;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    unsigned i;
#endif
    if (exec200_probe(&now, EXEC_REPLACE_QUERY)) fail("query", 0);
    if (memcmp(&now, &original, sizeof(now))) fail(what, (long)now.generation[0]);
    if (Pgetpid() != (long)original.pid) fail("PID after failure", Pgetpid());
    if (Fwrite((short)descriptor, 1, "a") != 1) fail("descriptor after failure", descriptor);
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (!baseline) return;
    snapshot(&after);
    for (i = 0; i < MLT_FIELDS; i++) if (before.value[i] != after.value[i]) {
        /* Retired startup state can take or return one slab page. */
        if (i == MLT_KHEAP_PAGES &&
            (before.value[i] - after.value[i] == 1 || after.value[i] - before.value[i] == 1))
            continue;
        printf("Pexec200 counter %u: %lu -> %lu after %s\r\n", i,
               (unsigned long)before.value[i], (unsigned long)after.value[i], what);
        Pterm(82);
    }
#endif
}
static void injected(unsigned kind, unsigned stage, long expected)
{
    long result;
    char message[48];
    exec200_probe(0, EXEC_REPLACE_ARM + (kind << 8) + stage);
    result = exec200_call("EXEC200B.TOS", "\0", environment);
    exec200_probe(0, EXEC_REPLACE_ARM);
    sprintf(message, "injected failure %u.%u", kind, stage);
    if (result != expected) fail(message, result);
    unchanged(message);
}
int main(void)
{
    char tail[128];
    unsigned stage;
    long result;
    int nested = (unsigned char)_base->p_cmdlin[0] >= 1 && _base->p_cmdlin[1] == 'N';
    if (exec200_probe(&original, EXEC_REPLACE_QUERY)) fail("initial query", 0);
    if (original.generation[0] || original.generation[1]) fail("initial generation", 1);
    if (original.pid != (unsigned long)Pgetpid()) fail("initial PID", Pgetpid());
    descriptor = Fcreate("EXEC200.TMP", 0);
    if (descriptor < 0) fail("descriptor open", descriptor);
    /* Warm every allocation cache a successful preparation touches, then
     * take the baseline every later failure is compared against. */
    injected(2, 10, -39L);
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    snapshot(&before);
    baseline = 1;
#endif
    if ((result = exec200_call("NO970.TOS", "\0", environment)) != -33L) fail("missing file", result);
    unchanged("missing file");
    if ((result = exec200_call("LOADFAIL.TOS", "\0", environment)) >= 0) fail("load failure", result);
    unchanged("load failure");
#if defined(__mc68000__)
    /* This subject itself is a classic PRG on m68k: never a replacement. */
    if ((result = exec200_call("EXEC200.TOS", "\0", environment)) != -66L) fail("PRG target", result);
    unchanged("PRG target");
#endif
    for (stage = 1; stage <= 4; stage++) injected(3, stage, -39L);
    for (stage = 1; stage <= 8; stage++) injected(1, stage, -39L);
    for (stage = 1; stage <= 10; stage++)
        injected(2, stage, stage == 6 || stage == 7 ? -36L : stage == 9 ? -326L : -39L);
    /* The genuine commit: same PROCESS, different executable and root. */
    sprintf(tail + 1, "1 %ld %lu %lu %lu %lu %ld %d", descriptor,
            (unsigned long)original.pid, (unsigned long)original.tid,
            (unsigned long)original.root[0], (unsigned long)original.root[1], (long)Pgetppid(), nested);
    tail[0] = (char)strlen(tail + 1);
    result = exec200_call("EXEC200B.TOS", tail, environment);
    fail("successful Pexec(200) returned to the old program", result);
    return 84;
}
