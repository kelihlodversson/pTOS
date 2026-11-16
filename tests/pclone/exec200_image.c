/* The replacement image of the Pexec(200) acceptance test (#970). Runs in
 * the subject's PROCESS: same PID, TID, parent and descriptors, a different
 * executable and root, and the committed-exec generation the tail names.
 * Generation 1 waits for the runner to observe the commit, then replaces
 * itself once more; generation 2 completes the PROCESS with status 70. */
#include "exec200_probe.h"
#include <mint/mintbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *reason, long value)
{
    printf("Pexec200 image FAIL: %s (%ld)\r\n", reason, value);
    Pterm(85);
}
int main(int argc, char **argv)
{
    EXEC_REPLACE_TEST_STATE state;
    unsigned long generation, pid, tid, root0, root1;
    long descriptor, parent, result;
    const char *value = getenv("EXEC");
    char tail[128];
    _DTA dta;
    if (argc != 8) fail("argument count", argc);
    generation = strtoul(argv[1], 0, 10); descriptor = atol(argv[2]);
    pid = strtoul(argv[3], 0, 10); tid = strtoul(argv[4], 0, 10);
    root0 = strtoul(argv[5], 0, 10); root1 = strtoul(argv[6], 0, 10);
    parent = atol(argv[7]);
    if (exec200_probe(&state, EXEC_REPLACE_QUERY)) fail("query", 0);
    if (state.pid != pid || Pgetpid() != (long)pid) fail("same PID", Pgetpid());
    if (state.tid != tid) fail("same TID", (long)state.tid);
    if (Pgetppid() != parent) fail("same parent", Pgetppid());
    if (state.generation[0] != generation || state.generation[1]) fail("generation", (long)state.generation[0]);
    if (state.root[0] == root0 && state.root[1] == root1) fail("root replaced", (long)root0);
    if (!value || strcmp(value, "970") || !getenv("SECOND")) fail("new environment", 0);
    if (Fwrite((short)descriptor, 1, "b") != 1) fail("descriptor survives exec", descriptor);
    if (generation == 1) {
        /* The runner observes the commit first: the PROCESS must not have
         * completed or become reapable through it. */
        Fsetdta(&dta);
        while (Fsfirst("GO970.TMP", 0)) (void)Syield();
        sprintf(tail + 1, "2 %ld %lu %lu %lu %lu %ld", descriptor, pid, tid,
                (unsigned long)state.root[0], (unsigned long)state.root[1], parent);
        tail[0] = (char)strlen(tail + 1);
        result = Pexec(200, "EXEC200B.TOS", tail, 0); /* inherits this environment */
        fail("second Pexec(200) returned", result);
    }
    if (Fclose((short)descriptor)) fail("descriptor close", descriptor);
    Cconws("native Pexec(200) same-PROCESS executable/root replacement: PASS\r\n");
    Pterm(70);
    return 86;
}
