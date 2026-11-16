/* Checks shared by every replacement image of the Pexec(200) test (#970).
 * The tail each program passes to the next one is
 *   "GEN FD PID TID ROOT0 ROOT1 PPID NESTED POS BASE"
 * where GEN is the committed-exec generation the receiver must see, ROOT the
 * replaced program's address-space root, POS the descriptor's position and
 * BASE the replaced program's basepage. */
#ifndef EXEC200_CHECK_H
#define EXEC200_CHECK_H
#include "exec200_probe.h"
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned long generation, pid, tid, root0, root1, base;
    long descriptor, parent, position;
    int nested;
} EXEC200_TAIL;

static void exec200_fail(const char *who, const char *reason, long value)
{
    printf("Pexec200 %s FAIL: %s (%ld)\r\n", who, reason, value);
    Pterm(85);
}

/* Everything a replaced program must find: the same PROCESS (PID, TID,
 * parent), the same descriptor at the same position, a new environment,
 * command tail, default DTA, root and basepage, and the next generation. */
static __attribute__((unused)) void exec200_verify(const char *who, EXEC200_TAIL *t)
{
    EXEC_REPLACE_TEST_STATE state;
    char line[128];
    unsigned length;
    const char *value = getenv("EXEC");
    if ((void *)Fgetdta() != (void *)_base->p_cmdlin) exec200_fail(who, "default DTA", (long)Fgetdta());
    length = (unsigned char)_base->p_cmdlin[0];
    if (!length || length > 125) exec200_fail(who, "command tail length", (long)length);
    memcpy(line, _base->p_cmdlin + 1, length); line[length] = 0;
    if (line[0] == 'F') line[0] = ' ';
    if (sscanf(line, "%lu %ld %lu %lu %lu %lu %ld %d %ld %lu", &t->generation, &t->descriptor,
               &t->pid, &t->tid, &t->root0, &t->root1, &t->parent, &t->nested,
               &t->position, &t->base) != 10) exec200_fail(who, "command tail", (long)length);
    if (exec200_probe(&state, EXEC_REPLACE_QUERY)) exec200_fail(who, "query", 0);
    if (state.pid != t->pid || Pgetpid() != (long)t->pid) exec200_fail(who, "same PID", Pgetpid());
    if (state.tid != t->tid) exec200_fail(who, "same TID", (long)state.tid);
    if (Pgetppid() != t->parent) exec200_fail(who, "same parent", Pgetppid());
    if (state.generation[0] != t->generation || state.generation[1])
        exec200_fail(who, "generation", (long)state.generation[0]);
    if (state.root[0] == t->root0 && state.root[1] == t->root1) exec200_fail(who, "root replaced", (long)t->root0);
    if ((unsigned long)_base == t->base) exec200_fail(who, "new basepage", (long)t->base);
    if (!value || strcmp(value, "970") || !getenv("SECOND")) exec200_fail(who, "new environment", 0);
    if (Fseek(0L, (short)t->descriptor, 1) != t->position) exec200_fail(who, "descriptor position", t->position);
    if (Fwrite((short)t->descriptor, 1, "b") != 1) exec200_fail(who, "descriptor survives exec", t->descriptor);
    t->root0 = state.root[0]; t->root1 = state.root[1];
}

/* Replace this program, telling the next one what to verify. */
static __attribute__((unused)) long exec200_next(const char *path, char mode, const EXEC200_TAIL *t,
                                                 const char *environment)
{
    char tail[128];
    sprintf(tail + 1, "%s%lu %ld %lu %lu %lu %lu %ld %d %ld %lu", mode == 'F' ? "F" : "",
            t->generation + 1, t->descriptor, t->pid, t->tid, t->root0, t->root1,
            t->parent, t->nested, Fseek(0L, (short)t->descriptor, 1), (unsigned long)_base);
    tail[0] = (char)strlen(tail + 1);
    return Pexec(200, path, tail, environment);
}
#endif
