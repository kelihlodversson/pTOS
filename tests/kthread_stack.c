/* Exercise the production scheduler-stack ownership helper. */
#include "emutos.h"
#include "procmem.h"
#include "kthread_context.h"
#include <assert.h>
#include <string.h>

static int fail, allocations, releases;
PADDR kstack_alloc(uintptr_t *top)
{
    if (fail) return 0;
    allocations++;
    *top = 0x9000;
    return 0x8000;
}
void kstack_free(PADDR physical)
{
    assert(physical == 0x8000);
    releases++;
}
int main(void)
{
    KTHREAD_STACK thread, detached;
    memset(&thread, 0, sizeof(thread));
    detached = kthread_stack_detach(NULL);
    assert(!detached.physical && !detached.top);
    assert(!kthread_stack_allocate(NULL));
    fail = 1;
    assert(!kthread_stack_allocate(&thread) && !thread.physical && !releases);
    fail = 0;
    assert(kthread_stack_allocate(&thread));
    assert(thread.physical == 0x8000 && thread.top == 0x9000);
    assert(!kthread_stack_allocate(&thread) && allocations == 1);
    detached = kthread_stack_detach(&thread);
    assert(!thread.physical && !thread.top && detached.physical == 0x8000);
    kthread_stack_release(&thread);
    assert(!releases);
    kthread_stack_release(&detached);
    assert(!detached.physical && !detached.top && releases == 1);
    kthread_stack_release(&thread);
    kthread_stack_release(NULL);
    assert(releases == 1);
    return 0;
}
