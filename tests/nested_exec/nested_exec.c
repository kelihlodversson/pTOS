/*
 * nested_exec.c - synchronous nested Pexec from a managed THREAD (#713)
 *
 * A PROCESS launched by synchronous Pexec from a managed THREAD runs on that
 * THREAD's scheduler continuation. The scheduler associates it with a THREAD
 * of its own, makes it the owner of the carrier while it runs, and restores
 * the enclosing owner when the architecture launcher has returned. This suite
 * drives that with real programs through the real launchers:
 *
 *   - parent -> child -> grandchild -> great-grandchild chains, every level a
 *     distinct PROCESS/PID and THREAD/TID, every level its own owner again
 *     after its child has gone, through normal return, Pterm, Pterm0 and a
 *     fault; cooperative checkpoints inside the innermost level;
 *   - an independent peer that stays runnable, and an asynchronous PROCESS
 *     that nests without completing early or twice;
 *   - every construction failure rolled back with nothing published;
 *   - exact identity and resource counts before, during and after.
 *
 * The counters and injections come from the regression kernels' query channel
 * (CONF_WITH_PRIVATE_CLONE_TEST); without it the chains still run and their
 * statuses are checked.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include <string.h>
#include "nested_query.h"

void test_nested_exec(void);

#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_USER_ASPACE)

#define CHAIN_LEVELS 3          /* child, grandchild, great-grandchild */
static const char environment[2] = { 0, 0 };

struct counts { long processes, threads, spaces, kstacks, live, depth, tid; };

static void settle(void)
{
    int turn;
    for (turn = 0; turn < 4; turn++) (void)Syield(); /* stopped stacks are reclaimed on root */
}
static void snapshot(struct counts *c)
{
    settle();
    c->processes = nested_query(NQ_PROCESSES);
    c->threads = nested_query(NQ_THREADS);
    c->spaces = nested_query(NQ_SPACES);
    c->kstacks = nested_query(NQ_KSTACKS);
    c->live = nested_query(NQ_LIVE);
    c->depth = nested_query(NQ_DEPTH);
    c->tid = nested_query(NQ_TID);
}
static int same(const struct counts *a, const struct counts *b)
{
    return a->processes == b->processes && a->threads == b->threads &&
           a->spaces == b->spaces && a->kstacks == b->kstacks &&
           a->live == b->live && a->depth == b->depth && a->tid == b->tid;
}
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
struct leaf { long pid, tid, basepage, depth; };
/* The innermost level is the execution the scheduler entered last. */
static void read_leaf(struct leaf *leaf)
{
    leaf->pid = nested_query(NQ_LAST_PID);
    leaf->tid = nested_query(NQ_LAST_TID);
    leaf->depth = nested_query(NQ_LAST_DEPTH);
    leaf->basepage = nested_query(NQ_LAST_PD);
}

static long run_chain(int tracked, char mode, char flag, const struct counts *base)
{
    struct nested_chain chain;
    char tail[NESTED_TAIL_BYTES];
    chain.remaining = CHAIN_LEVELS - 1;
    chain.mode = mode;
    chain.flag = flag;
    chain.tracked = tracked;
    chain.ppid = Pgetpid();
    chain.ptid = base->tid;
    chain.depth = tracked ? (int)base->depth + 1 : 0;
    chain.level = 1;
    chain.processes = base->processes; chain.threads = base->threads;
    chain.spaces = base->spaces; chain.kstacks = base->kstacks;
    nested_chain_tail(tail, &chain);
    return Pexec(0, NESTED_CHILD, tail, (void *)environment);
}

static void test_chains(int tracked)
{
    static const char modes[] = { 'r', 't', '0', 'f' };
    struct counts base, now;
    struct leaf leaf, previous;
    long pid = Pgetpid(), status;
    long prepared, reclaimed, stopped;
    unsigned mode;
    int round;

    /* The first chain backs this PROCESS's own lazy pages and file buffers. */
    snapshot(&base);
    status = run_chain(tracked, 't', '-', &base);
    ptest_assert_msg(status == NESTED_LEVEL_STATUS(CHAIN_LEVELS - 1), "warm-up chain returns its level status");
    read_leaf(&previous);
    snapshot(&base);
    prepared = nested_query(NQ_PREPARED);
    stopped = nested_query(NQ_STOPPED);
    reclaimed = nested_query(NQ_RECLAIMED);

    for (mode = 0; mode < sizeof(modes); mode++) {
        status = run_chain(tracked, modes[mode], tracked ? 'x' : '-', &base);
        ptest_assert_msg(status == NESTED_LEVEL_STATUS(CHAIN_LEVELS - 1),
                         "every level verified its identity, its child's status and its own restoration");
        ptest_assert_msg(Pgetpid() == pid && Pwaitpid(-1, 1, 0) == -33L,
                         "the launcher keeps its PROCESS and gains no waitable child");
        if (tracked) {
            read_leaf(&leaf);
            ptest_assert_msg(leaf.pid > 0 && leaf.pid != pid && leaf.pid != previous.pid &&
                             leaf.tid > 0 && leaf.tid != base.tid && leaf.tid != previous.tid &&
                             leaf.depth == base.depth + CHAIN_LEVELS,
                             "the innermost level ran as a new PROCESS and THREAD, three associations deep");
            snapshot(&now);
            ptest_assert_msg(same(&base, &now), "identities, address spaces and stacks return to the baseline");
            previous = leaf;
        }
    }
    if (tracked) {
        ptest_assert_msg(nested_query(NQ_PREPARED) - prepared == CHAIN_LEVELS * (long)sizeof(modes) &&
                         nested_query(NQ_STOPPED) - stopped == CHAIN_LEVELS * (long)sizeof(modes) &&
                         nested_query(NQ_RECLAIMED) - reclaimed == CHAIN_LEVELS * (long)sizeof(modes),
                         "each nested THREAD was stopped once and reclaimed once");
    }

    /* Repeated launches reuse the same basepage storage for new identities:
     * an association is bound to the retained PROCESS, never to that address. */
    for (round = 0; round < 8; round++) {
        status = run_chain(tracked, round & 1 ? 't' : 'r', tracked ? 'x' : '-', &base);
        ptest_assert_msg(status == NESTED_LEVEL_STATUS(CHAIN_LEVELS - 1), "repeated chain passes");
        if (tracked) {
            read_leaf(&leaf);
            ptest_assert_msg(leaf.pid != previous.pid && leaf.tid > previous.tid,
                             "a repeated launch never reuses a PROCESS or THREAD identity");
            ptest_assert_msg(leaf.basepage == previous.basepage, "while the basepage storage itself is reused");
            previous = leaf;
        }
    }
    if (tracked) {
        snapshot(&now);
        ptest_assert_msg(same(&base, &now), "repeated chains conserve every counted resource");
    }
}

static void test_peers(int tracked)
{
    static const char peer_tail[] = "\1p", carrier_tail[] = "\1a";
    struct counts base, now;
    long peer, carrier, status;

    (void)Fdelete("C:\\NESTPEER.ON");
    (void)Fdelete("C:\\NESTGO.ON");
    snapshot(&base);
    peer = Pexec(100, NESTED_CHILD, peer_tail, (void *)environment);
    ptest_assert_msg(peer > 0, "independent peer admitted");
    if (peer <= 0) return;
    /* The innermost level only continues once the peer has run: the peer
     * is dispatched while three nested executions own this carrier. */
    status = run_chain(tracked, 't', 'p', &base);
    ptest_assert_msg(status == NESTED_LEVEL_STATUS(CHAIN_LEVELS - 1) && exists("C:\\NESTPEER.ON"),
                     "a peer runs while the nested chain is active, and the chain resumes as itself");
    ptest_assert_msg(Pwaitpid((short)peer, 1, 0) == 0, "the peer is still live after the chain has gone");

    /* A managed PROCESS of its own nests a child: that child's completion
     * is not this PROCESS's, which completes once, later. */
    carrier = Pexec(100, NESTED_CHILD, carrier_tail, (void *)environment);
    ptest_assert_msg(carrier > 0, "asynchronous nesting PROCESS admitted");
    if (carrier > 0) {
        int turn;
        for (turn = 0; turn < 64; turn++) {
            (void)Syield();
            ptest_assert_msg(Pwaitpid((short)carrier, 1, 0) == 0,
                             "a nested child's exit does not complete its carrier's PROCESS");
        }
    }
    mark("C:\\NESTGO.ON");
    if (carrier > 0) {
        status = Pwaitpid((short)carrier, 0, 0);
        ptest_assert_msg((status >> 16) == carrier && (status & 0xffff) == 77,
                         "the carrier PROCESS completes once with its own status");
        ptest_assert_msg(Pwaitpid((short)carrier, 1, 0) == -33L, "and is consumed exactly once");
    }
    status = Pwaitpid((short)peer, 0, 0);
    ptest_assert_msg((status >> 16) == peer && (status & 0xffff) == 55,
                     "the independent peer completes with its own status");
    (void)Fdelete("C:\\NESTPEER.ON");
    (void)Fdelete("C:\\NESTGO.ON");
    if (tracked) {
        snapshot(&now);
        ptest_assert_msg(same(&base, &now), "peers and their nested children leave the baseline intact");
    }
}

#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static void test_rollback(void)
{
    static const char marker_tail[] = "\1m";
    static const char *const stages[] = {
        "association record allocation",
        "PROCESS identity retention",
        "initial THREAD construction",
        "syscall stack allocation",
        "address-space retention",
        "launch abandoned after preparation",
    };
    struct counts base, now;
    long status, refused, cancelled;
    unsigned stage;

    (void)Fdelete("C:\\NESTRAN.ON");
    status = Pexec(0, NESTED_CHILD, marker_tail, (void *)environment);
    ptest_assert_msg(status == 0 && exists("C:\\NESTRAN.ON"), "the marker child runs when nothing is injected");
    (void)Fdelete("C:\\NESTRAN.ON");
    snapshot(&base);
    for (stage = 1; stage <= 6; stage++) {
        refused = nested_query(NQ_REFUSED);
        cancelled = nested_query(NQ_CANCELLED);
        ptest_assert_msg(nested_query(NQ_FAIL_STAGE + stage) == 0, "arm one construction failure");
        status = Pexec(0, NESTED_CHILD, marker_tail, (void *)environment);
        ptest_assert_msg(status == NESTED_ENSMEM, stages[stage - 1]);
        ptest_assert_msg(nested_query(NQ_REFUSED) == refused + 1, "the injected failure was the one reached");
        ptest_assert_msg(nested_query(NQ_CANCELLED) == cancelled + (stage == 6),
                         "only a completed preparation needs cancelling");
        ptest_assert_msg(!exists("C:\\NESTRAN.ON") && Pwaitpid(-1, 1, 0) == -33L,
                         "a failed construction runs and publishes nothing");
        snapshot(&now);
        ptest_assert_msg(same(&base, &now), "a failed construction leaves no identity, space or stack");
        /* and the very next launch is unaffected */
        status = Pexec(0, NESTED_CHILD, marker_tail, (void *)environment);
        ptest_assert_msg(status == 0 && exists("C:\\NESTRAN.ON"), "the launch after a rollback succeeds");
        (void)Fdelete("C:\\NESTRAN.ON");
        snapshot(&now);
        ptest_assert_msg(same(&base, &now), "and conserves the baseline too");
    }
}
#endif

void test_nested_exec(void)
{
    static const char identity_tail[] = "\1i";
    int tracked = 0;

    ptest_begin("nested_exec (#713)");
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    /* The refused-handoff regression keeps one launch per boot without a
     * THREAD; it may be this one. The second is then associated. */
    if (nested_query(NQ_TID) > 0) {
        long first = Pexec(0, NESTED_CHILD, identity_tail, (void *)environment);
        if (first == 2) first = Pexec(0, NESTED_CHILD, identity_tail, (void *)environment);
        ptest_assert_msg(first == 1, "a managed THREAD's synchronous child is an associated THREAD");
        tracked = first == 1;
    }
#else
    (void)identity_tail;
#endif
    test_chains(tracked);
    test_peers(tracked);
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (tracked) test_rollback();
#endif
    ptest_pass();
    if (!ptest_failures() && tracked) {
        conws("native nested Pexec identity/exit modes/restoration: PASS\r\n");
        conws("native nested Pexec construction rollback: PASS\r\n");
        conws("native nested Pexec resource conservation: PASS\r\n");
    }
}

#else

void test_nested_exec(void)
{
    ptest_begin("nested_exec (#713)");
    ptest_pass();   /* no managed THREADs: nothing to associate */
}

#endif
