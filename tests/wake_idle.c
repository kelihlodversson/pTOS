/* Production wake admission state; deterministic interrupt-order injection,
 * not a hosted CPU instruction/IRQ proof. Native probes test real idle. */
#include "emutos.h"
#include "ksched_wake.h"
#include <stdio.h>
extern void abort(void) __attribute__((noreturn));
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"wake:%d: %s\n",__LINE__,#x); abort(); } } while (0)
static void *fatal[5];
static BOOL expected;
void panic(const char *message,...)
{ (void)message; if (expected) __builtin_longjmp(fatal,1); abort(); }
int main(void)
{
    KSCHED_WAKE_STATE state={0,FALSE};
    UQUAD scan;
    unsigned i;
    for (i=0;i<128;i++) {
        scan=state.sequence;
        ksched_wake_publish(&state); /* producer in check-to-idle gap */
        CHECK(!ksched_wake_arm(&state,scan) && !state.armed);
        scan=state.sequence;
        CHECK(ksched_wake_arm(&state,scan) && state.armed);
        /* Unrelated IRQ returns from idle without publishing this owner.
         * Rescan/disarm/rearm is valid; the owner is still not satisfied. */
        CHECK(state.sequence==scan && state.armed);
        ksched_wake_disarm(&state);
        CHECK(ksched_wake_arm(&state,scan));
        ksched_wake_publish(&state); /* owner IRQ after atomic admission */
        CHECK(!state.armed && state.sequence!=scan);
        scan=state.sequence;
        CHECK(ksched_wake_arm(&state,scan));
        ksched_wake_disarm(&state); /* root park/cancel, no borrowed owner */
        ksched_wake_publish(&state); /* event while root is detached */
        CHECK(!ksched_wake_arm(&state,scan) && !state.armed);
    }
    scan=state.sequence;
    CHECK(ksched_wake_arm(&state,scan));
    expected=TRUE;
    if (!__builtin_setjmp(fatal)) { ksched_wake_arm(&state,scan); abort(); }
    expected=FALSE;
    CHECK(state.armed && state.sequence==scan);
    ksched_wake_disarm(&state);
    state.sequence=~(UQUAD)0;
    expected=TRUE;
    if (!__builtin_setjmp(fatal)) { ksched_wake_publish(&state); abort(); }
    expected=FALSE;
    CHECK(state.sequence==~(UQUAD)0 && !state.armed);
    puts("Production wake/idle state: before/after admission, park, duplicate arm and no-wrap: PASS (host IRQ model)");
    return 0;
}
