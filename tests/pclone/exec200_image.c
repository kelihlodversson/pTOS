/* The ELF replacement image of the Pexec(200) acceptance test (#970). It runs
 * in the subject's PROCESS (exec200_check.h lists what it verifies).
 * Generation 1 replaces itself once more: after the runner has observed the
 * commit when the subject is an anchor (a nested subject's runner is blocked
 * in its Pexec(0)), and leaving a directory search open. Generation 2 checks
 * that search did not survive and replaces itself with EXEC200.TOS, a
 * classic PRG on m68k, which completes the PROCESS. */
#include "exec200_check.h"
#include <mint/mintbind.h>

int main(void)
{
    EXEC200_TAIL t;
    long result;
    exec200_verify("image", &t);
    if (t.generation == 1) {
        while (!t.nested && Fsfirst("GO970.TMP", 0)) (void)Syield();
        /* Leave a multi-match search open on the default DTA. */
        if (Fsfirst("*.TOS", 0)) exec200_fail("image", "open search", 0);
        result = exec200_next("EXEC200B.TOS", 0, &t, 0);   /* inherits this environment */
        exec200_fail("image", "second Pexec(200) returned", result);
    }
    if (t.generation != 2) exec200_fail("image", "unexpected generation", (long)t.generation);
    /* The replaced program's directory search must not survive: it was
     * left open on this same default DTA, address and PROCESS. */
    if (Fsnext() == 0) exec200_fail("image", "search survived the exec", 0);
    result = exec200_next("EXEC200.TOS", 'F', &t, 0);
    exec200_fail("image", "Pexec(200) of the subject returned", result);
    return 86;
}
