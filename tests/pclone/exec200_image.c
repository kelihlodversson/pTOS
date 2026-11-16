/* The replacement image of the Pexec(200) acceptance test (#970). Runs in
 * the subject's PROCESS: same PID, TID, parent and descriptors, a different
 * executable and root, and the committed-exec generation the tail names.
 * Generation 1 replaces itself once more, after the runner has observed the
 * commit when the subject is an anchor (a nested subject's runner is blocked
 * in its Pexec(0)); generation 2 completes the PROCESS with status 70. */
#include "exec200_probe.h"
#include <mint/mintbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/basepage.h>

static void fail(const char *reason, long value)
{
    printf("Pexec200 image FAIL: %s (%ld)\r\n", reason, value);
    Pterm(85);
}
int main(void)
{
    EXEC_REPLACE_TEST_STATE state;
    unsigned long generation, pid, tid, root0, root1;
    long descriptor, parent, result;
    int nested;
    const char *value = getenv("EXEC");
    char tail[128], line[128];
    unsigned length;
    _DTA dta;
    /* The tail the commit copied into this PROCESS's basepage (libcmini's
     * minimal startup does not split it into argv). */
    length = (unsigned char)_base->p_cmdlin[0];
    if (!length || length > 125) fail("command tail length", (long)length);
    memcpy(line, _base->p_cmdlin + 1, length); line[length] = 0;
    if (sscanf(line, "%lu %ld %lu %lu %lu %lu %ld %d", &generation, &descriptor, &pid, &tid,
               &root0, &root1, &parent, &nested) != 8) fail("command tail", (long)length);
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
        while (!nested && Fsfirst("GO970.TMP", 0)) (void)Syield();
        sprintf(tail + 1, "2 %ld %lu %lu %lu %lu %ld %d", descriptor, pid, tid,
                (unsigned long)state.root[0], (unsigned long)state.root[1], parent, nested);
        tail[0] = (char)strlen(tail + 1);
        result = Pexec(200, "EXEC200B.TOS", tail, 0); /* inherits this environment */
        fail("second Pexec(200) returned", result);
    }
    if (Fclose((short)descriptor)) fail("descriptor close", descriptor);
    Cconws(nested ? "native nested Pexec(200) launcher relaunch: PASS\r\n"
                  : "native Pexec(200) same-PROCESS executable/root replacement: PASS\r\n");
    Pterm(70);
    return 86;
}
