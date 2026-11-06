/* Published startup teardown enforces root ownership and no disk-error unwind.
 * Kernel panic is captured only by this host fixture, never recovered in vivo. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../../bdos/thread_startup.c"
#define CHECK(c) do { if (!(c)) abort(); } while (0)
static THREAD_STARTUP_DRAFT record;
static THREAD_STARTUP_DRAFT *handle;
static UWORD phase, released, freed;
static jmp_buf fatal, original;
jmp_buf errbuf;
BOOL ksched_startup_reclaimable(const THREAD_STARTUP_DRAFT *draft)
{ CHECK(draft == &record); return phase != 2; }
void kaspace_lease_release(KASPACE_LEASE **lease) { CHECK(!*lease); }
void kaspace_put(KASPACE *space) { CHECK(!space); }
void thread_startup_arch_abort(void *image) { CHECK(!image); }
void thread_startup_arch_bridge_release(void **bridge) { CHECK(!*bridge); }
void kfree(void *p) { CHECK(p == &record); freed++; }
static void observer_release(void *context)
{
    CHECK(context == &record && !released); released++;
    if (phase == 1) longjmp(errbuf,1);
}
void panic(const char *fmt, ...)
{
    CHECK(phase && ((phase == 1 && strstr(fmt,"cleanup callback escaped")) ||
                   (phase == 2 && strstr(fmt,"outside stopped scheduler root")) ||
                   (phase == 3 && strstr(fmt,"abort after publication"))));
    longjmp(fatal,1);
}
int main(void)
{
    for (phase = 0; phase < 4; phase++) {
        memset(&record,0,sizeof(record)); record.state = THREAD_STARTUP_RUNNABLE;
        record.observer.release = observer_release; record.observer_context = &record;
        handle = &record; released = freed = 0;
        memset(errbuf,0x5a,sizeof(errbuf)); memcpy(original,errbuf,sizeof(errbuf));
        if (!setjmp(fatal)) {
            if (phase == 3) thread_startup_abort(&handle);
            else thread_startup_reclaim(&handle);
            CHECK(!phase);
        } else CHECK(phase);
        CHECK(!memcmp(original,errbuf,sizeof(errbuf)));
        CHECK(released == (phase <= 1 ? 1 : 0) && freed == (phase == 0 ? 1 : 0));
        if (phase <= 1) {
            CHECK(!handle);
            thread_startup_abort(&handle); /* consumed even on fatal callback */
            CHECK(released == 1);
        } else CHECK(handle == &record);
    }
    puts("Published startup root ownership and callback fail-stop: PASS");
    return 0;
}
