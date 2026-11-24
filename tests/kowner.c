/* Production owner domains; CPU/IRQ mechanics are explicit hosted endpoints. */
#include "emutos.h"
#include "kowner.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

static KOWNER_CPU cpus[2];
static unsigned selected;
static uintptr_t flags=0x200;
static BOOL expect_panic;
KOWNER_CPU *kowner_cpu(void) { return &cpus[selected]; }
uintptr_t kowner_irq_save(void) { uintptr_t saved=flags;flags=0;return saved; }
void kowner_irq_restore(uintptr_t saved) { flags=saved; }
void panic(const char *message,...)
{ (void)message;if(expect_panic)_exit(86);abort(); }
static void failure(unsigned scenario)
{
    KOWNER_GUARD process,mapping,ignored;
    expect_panic=TRUE;
    if(scenario==4)cpus[0].clock=~(UQUAD)0;
    kowner_enter(&process,KOWNER_PROCESS);
    if(scenario==0) { kowner_enter(&mapping,KOWNER_MAPPING);(void)mapping;kowner_leave(&process); }
    if(scenario==1) { kowner_enter(&mapping,KOWNER_RUNQUEUE);(void)mapping;kowner_enter(&ignored,KOWNER_PROCESS); }
    if(scenario==2) { selected=1;kowner_leave(&process); }
    if(scenario==3)kowner_assert_unlocked();
    if(scenario==5) { kowner_leave(&process);kowner_enter(&ignored,KOWNER_PROCESS);kowner_leave(&process); }
    if(scenario==6) { selected=1;kowner_enter(&ignored,KOWNER_PROCESS); }
    _exit(1);
}
int main(void)
{
    unsigned i;
    for(i=0;i<128;i++) {
        KOWNER_GUARD process; kowner_enter(&process,KOWNER_PROCESS);
        KOWNER_GUARD mapping; kowner_enter(&mapping,KOWNER_MAPPING);
        KOWNER_GUARD admission; kowner_enter(&admission,KOWNER_ADMISSION);
        KOWNER_GUARD leaf; kowner_enter(&leaf,KOWNER_RUNQUEUE);
        assert(!flags && kowner_locked() && kowner_holds(KOWNER_PROCESS));
        kowner_leave(&leaf);kowner_leave(&admission);kowner_leave(&mapping);
        assert(kowner_holds(KOWNER_PROCESS) && !kowner_holds(KOWNER_MAPPING));
        kowner_leave(&process);
        assert(flags==0x200 && !kowner_locked());
    }
    for(i=0;i<7;i++) {
        int status;
        pid_t child=fork();assert(child>=0);
        if(!child)failure(i);
        assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==86);
    }
    puts("Owner domains: ordered nesting, exact IRQ restoration, stale/LIFO/rank/CPU rejection, exhaustion and no handoff while locked: PASS (CPU model)");
    return 0;
}
