/* NESTCH.TOS: one level of a real synchronous nested Pexec chain (#713).
 * Small on purpose, like GROUPCH.TOS: it is launched recursively. Every
 * failed check ends the level with a distinct status >= 90. */
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include "nested_query.h"

static const char environment[2] = { 0, 0 };

#if defined(__mc68000__) && defined(TEST_IS_PRG)
/* Each chain level has a separate TPA. The bounded helper uses <1 KiB of
 * C frames; declare its stack budget so an admitted, not-yet-started peer's
 * unshrunk TPA does not force this CRT to request the default 64 KiB. */
long _stksize = 16L * 1024;
#endif

static int exists(const char *path)
{
    long handle = Fopen(path, 0);
    if (handle < 0) return 0;
    (void)Fclose(handle);
    return 1;
}
static void mark(const char *path)
{
    long handle = Fcreate(path, 0);
    if (handle >= 0) (void)Fclose(handle);
}

/* Identity of this execution, as the kernel and its scheduler see it. */
static int identity_is(const struct nested_chain *chain, long pid, long tid)
{
    if (Pgetpid() != pid || Pgetppid() != chain->ppid) return 0;
    if (!chain->tracked) return 1;
    return nested_query(NQ_TID) == tid && nested_query(NQ_DEPTH) == chain->depth;
}
static int counts_are(const struct nested_chain *chain, long extra)
{
    static const unsigned selectors[] = { NQ_PROCESSES, NQ_THREADS, NQ_SPACES, NQ_KSTACKS,
        NQ_LIVE, NQ_IDENTITIES, NQ_PROCREFS, NQ_SPACEREFS, NQ_SCHEDSTACKS };
    long level = chain->level + extra;
    long expected[9];
    unsigned i;
    if (chain->flag != 'x') return 1;
    expected[0] = chain->processes + level; expected[1] = chain->threads + level;
    expected[2] = chain->spaces + level; expected[3] = chain->kstacks + level;
    expected[4] = chain->depth + extra; expected[5] = chain->identities + level;
    /* Each level: live lookup, frame, THREAD, enclosing frame retain and
     * the launch's retained logical parent identity. */
    expected[6] = chain->procrefs + 5 * level;
    /* Each nested PROCESS owns active-space, initial THREAD and ordinary
     * mapping membership references. All three disappear after its stop. */
    expected[7] = chain->spacerefs + 3 * level;
    expected[8] = chain->schedstacks;
    for (i = 0; i < 9; i++) {
        long actual = nested_query(selectors[i]);
        if (actual != expected[i]) {
            char message[] = "nested count 000 actual 00000000 expected 00000000\r\n";
            nested_put_hex(message + 13, selectors[i], 3);
            nested_put_hex(message + 24, (unsigned long)actual, 8);
            nested_put_hex(message + 42, (unsigned long)expected[i], 8);
            Cconws(message);
            return 0;
        }
    }
    return 1;
}

static int run_chain(const char *tail)
{
    struct nested_chain chain, next;
    long pid = Pgetpid(), tid = nested_query(NQ_TID), status;
    int spin;

    chain.remaining = tail[1] - '0';
    chain.mode = tail[2];
    chain.flag = tail[3];
    chain.tracked = tail[4] == 'T';
    chain.ppid = (long)nested_get_hex(tail + 5, 4);
    chain.ptid = (long)nested_get_hex(tail + 9, 8);
    chain.depth = (int)nested_get_hex(tail + 17, 2);
    chain.level = (int)nested_get_hex(tail + 19, 2);
    chain.processes = (long)nested_get_hex(tail + 21, 4);
    chain.threads = (long)nested_get_hex(tail + 25, 4);
    chain.spaces = (long)nested_get_hex(tail + 29, 4);
    chain.kstacks = (long)nested_get_hex(tail + 33, 4);

    chain.identities = (long)nested_get_hex(tail + 37, 4);
    chain.procrefs = (long)nested_get_hex(tail + 41, 4);
    chain.spacerefs = (long)nested_get_hex(tail + 45, 4);
    chain.schedstacks = (long)nested_get_hex(tail + 49, 4);

    /* A distinct PROCESS and, where the launcher is a managed THREAD, a
     * distinct THREAD of its own, one association deeper. */
    if (pid <= 0 || pid == chain.ppid) return 90;
    if (chain.tracked && (tid <= 0 || tid == chain.ptid)) return 91;
    if (!chain.tracked && tid != NESTED_EINVFN) return 92;
    if (!identity_is(&chain, pid, tid)) return 93;
    if (!counts_are(&chain, 0)) return 94;
    /* It stays a single-THREAD PROCESS without a scheduler anchor. */
    if (nested_trap2(GEMDOS_PTHJOIN, tid, 0L) != NESTED_EINVFN) return 95;
    if (nested_trap2(GEMDOS_PTERMGRP, 78L, 0L) != NESTED_EINVFN) return 96;
    if (Pwaitpid(-1, 1, 0) != -33L) return 97;

    if (chain.remaining > 0) {
        char next_tail[NESTED_TAIL_BYTES];
        long expected;
        next = chain;
        next.remaining = chain.remaining - 1;
        next.ppid = pid;
        next.ptid = tid;
        next.depth = chain.depth + (chain.tracked ? 1 : 0);
        next.level = chain.level + 1;
        nested_chain_tail(next_tail, &next);
        expected = next.remaining ? NESTED_LEVEL_STATUS(next.remaining) : nested_leaf_status(chain.mode);
        status = Pexec(0, NESTED_CHILD, next_tail, (void *)environment);
        if (status != expected) return 98;
        /* The child is gone, with everything it owned, and this execution
         * is the owner of its carrier again. */
        if (!identity_is(&chain, pid, tid)) return 99;
        if (!counts_are(&chain, 0)) return 100;
        if (Pwaitpid(-1, 1, 0) != -33L) return 101;
        (void)Syield();
        if (!identity_is(&chain, pid, tid)) return 102;
        return (int)NESTED_LEVEL_STATUS(chain.remaining);
    }

    /* The leaf reaches real cooperative checkpoints while it is the
     * innermost owner: every resumption must find this identity again. */
    if (chain.flag == 'p') {
        for (spin = 0; spin < 20000 && !exists("C:\\NESTPEER.ON"); spin++) {
            (void)Syield();
            if (!identity_is(&chain, pid, tid)) return 103;
        }
        if (!exists("C:\\NESTPEER.ON")) return 104;
    }
    for (spin = 0; spin < 4; spin++) {
        (void)Syield();
        if (!identity_is(&chain, pid, tid)) return 105;
    }
    /* The scheduler entered this execution last, with these identities. */
    if (chain.tracked && (nested_query(NQ_LAST_PID) != pid || nested_query(NQ_LAST_TID) != tid ||
        nested_query(NQ_LAST_DEPTH) != chain.depth ||
        nested_query(NQ_LAST_PD) != (long)((unsigned long)_base & 0x7fffffffUL))) return 106;
    if (chain.mode == 't') Pterm(24);
    if (chain.mode == '0') Pterm0();
    if (chain.mode == 'f') {
        *(volatile unsigned long *)0x80001000UL = 1; /* kernel half: ends this PROCESS only */
        return 107;
    }
    return 23;
}

int main(void)
{
    const char *tail = (const char *)_base->p_cmdlin;
    int spin;

    switch (tail[1]) {
    case 'c':
        return run_chain(tail + 1); /* a level that passes returns normally */
    case 'i':               /* is this launch an associated THREAD? */
        return nested_query(NQ_TID) > 0 ? 1 : 2;
    case 'm':               /* proof of execution for the rollback checks */
        mark("C:\\NESTRAN.ON");
        return 0;
    case 'p':               /* an independent peer, runnable throughout */
        mark("C:\\NESTPEER.ON");
        for (spin = 0; spin < 20000 && !exists("C:\\NESTGO.ON"); spin++) (void)Syield();
        Pterm(exists("C:\\NESTGO.ON") ? 55 : 56);
        return 109;
    case 'a': {             /* a managed PROCESS of its own that nests */
        struct nested_chain chain;
        char next_tail[NESTED_TAIL_BYTES];
        long pid = Pgetpid(), tid = nested_query(NQ_TID);
        chain.remaining = 1; chain.mode = 't'; chain.flag = '-';
        chain.tracked = tid > 0;
        chain.ppid = pid; chain.ptid = tid;
        chain.depth = chain.tracked ? (int)nested_query(NQ_DEPTH) + 1 : 0;
        chain.level = 1;
        chain.processes = chain.threads = chain.spaces = chain.kstacks = 0;
        chain.identities = chain.procrefs = chain.spacerefs = chain.schedstacks = 0;
        nested_chain_tail(next_tail, &chain);
        if (Pexec(0, NESTED_CHILD, next_tail, (void *)environment) != NESTED_LEVEL_STATUS(1)) Pterm(110);
        if (Pgetpid() != pid || nested_query(NQ_TID) != tid) Pterm(111);
        for (spin = 0; spin < 20000 && !exists("C:\\NESTGO.ON"); spin++) (void)Syield();
        Pterm(exists("C:\\NESTGO.ON") ? 77 : 76);
        return 112;
    }
    default:
        return 113;
    }
}
