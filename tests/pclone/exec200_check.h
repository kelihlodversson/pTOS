/* Checks shared by every replacement image of the Pexec(200) test (#970).
 * The tail each program passes to the next one is
 *   "GEN FD PID TID ROOT0 ROOT1 PPID NESTED POS BASE PROCESS0 PROCESS1"
 * where GEN is the committed-exec generation the receiver must see, ROOT the
 * replaced program's address-space root, POS the descriptor's position and
 * BASE the replaced program's basepage. */
#ifndef EXEC200_CHECK_H
#define EXEC200_CHECK_H
#include "exec200_probe.h"
#include "../entry_args.h"
#include <mint/mintbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__mc68000__) && !defined(TEST_WITH_ELF_LOADER)
#define EXEC200_TARGET "EXEC200P.TOS"
#else
#define EXEC200_TARGET "EXEC200B.TOS"
#endif

typedef struct {
    unsigned long generation, pid, tid, root0, root1, base;
    unsigned long process0, process1;
    long descriptor, parent, position;
    int nested;
} EXEC200_TAIL;

static void exec200_fail(const char *who, const char *reason, long value)
{
    printf("Pexec200 %s FAIL: %s (%ld)\r\n", who, reason, value);
    Pterm(85);
}

static void exec200_verify_startup(const char *who)
{
#if defined(__mc68000__)
    unsigned long stack = (unsigned long)&who;
    unsigned long low = (unsigned long)_base->p_lowtpa;
    unsigned long high = (unsigned long)_base->p_hitpa;
    unsigned long text = (unsigned long)_base->p_tbase;
    /* EXEC200B is linked as ELF even when the default tests are PRGs. */
#if defined(TEST_IS_PRG) && defined(EXEC200_PRG)
    if (low != (unsigned long)_base || text != low + 256 || stack < text || stack >= high)
        exec200_fail(who, "classic PRG TPA/text/stack", (long)stack);
#else
    if (low != (unsigned long)_base || (text >= low && text < high) ||
        (stack >= low && stack < high))
        exec200_fail(who, "ELF text and private stack", (long)stack);
#endif
#endif
    if ((void *)Fgetdta() != (void *)_base->p_cmdlin) exec200_fail(who, "default DTA", (long)Fgetdta());
}

/* Everything a replaced program must find: the same PROCESS (PID, TID,
 * parent), the same descriptor at the same position, a new environment,
 * command tail, default DTA, root and basepage, and the next generation. */
static __attribute__((unused)) void exec200_verify(const char *who, const char *argument, EXEC200_TAIL *t)
{
    char line[128];
    unsigned length;
    const char *value = getenv("EXEC");
    EXEC_REPLACE_TEST_STATE state;
    exec200_verify_startup(who);
    length = strlen(argument);
    if (!length || length > 125) exec200_fail(who, "command tail length", (long)length);
    memcpy(line, argument, length + 1);
    if (line[0] == 'F') line[0] = ' ';
    if (sscanf(line, "%lu %ld %lu %lu %lu %lu %ld %d %ld %lu %lu %lu", &t->generation, &t->descriptor,
               &t->pid, &t->tid, &t->root0, &t->root1, &t->parent, &t->nested,
               &t->position, &t->base, &t->process0, &t->process1) != 12)
        exec200_fail(who, "command tail", (long)length);
    if (exec200_probe(&state, EXEC_REPLACE_QUERY)) exec200_fail(who, "query", 0);
    if (state.pid != t->pid || Pgetpid() != (long)t->pid) exec200_fail(who, "same PID", Pgetpid());
    if (state.tid != t->tid) exec200_fail(who, "same TID", (long)state.tid);
    if (state.process[0] != t->process0 || state.process[1] != t->process1)
        exec200_fail(who, "same PROCESS identity", (long)state.process[0]);
    if (Pgetppid() != t->parent) exec200_fail(who, "same parent", Pgetppid());
    if (state.generation[0] != t->generation || state.generation[1])
        exec200_fail(who, "generation", (long)state.generation[0]);
    if (state.root[0] == t->root0 && state.root[1] == t->root1) exec200_fail(who, "root replaced", (long)t->root0);
    if ((unsigned long)_base == t->base) exec200_fail(who, "new basepage", (long)t->base);
    /* The parent link the new basepage took must be readable in the new
     * root (the ancestors page), not a dangling address of the old one. */
    if (_base->p_parent) (void)*(volatile const char *)_base->p_parent;
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
#ifdef TEST_NATIVE_ELF_ENTRY
    char *const argv[] = { (char *)path, tail + 1, NULL };
    char *explicit_env[] = { (char *)environment, NULL, NULL };
    extern char **environ;
#endif
    int length=snprintf(tail + 1, sizeof(tail) - 1, "%s%lu %ld %lu %lu %lu %lu %ld %d %ld %lu %lu %lu",
            mode == 'F' ? "F" : "", t->generation + 1, t->descriptor, t->pid, t->tid, t->root0, t->root1,
            t->parent, t->nested, Fseek(0L, (short)t->descriptor, 1), (unsigned long)_base,
            t->process0, t->process1);
    if (length <= 0 || length > 125) exec200_fail("image", "encode command tail", length);
#ifdef TEST_NATIVE_ELF_ENTRY
    if (environment)
        explicit_env[1] = (char *)environment + strlen(environment) + 1;
    return Pexecve(200, path, argv, environment ? explicit_env : environ);
#else
    tail[0] = (char)length;
    return Pexec(200, path, tail, environment);
#endif
}
#endif
