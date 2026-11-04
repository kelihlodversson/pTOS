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
    KTHREAD_CONTEXT thread;
    memset(&thread, 0, sizeof(thread));
    assert(!kthread_stack_allocate(NULL));
    fail = 1;
    assert(!kthread_stack_allocate(&thread) && !thread.stack_phys && !releases);
    fail = 0;
    assert(kthread_stack_allocate(&thread));
    assert(thread.stack_phys == 0x8000 && thread.stack_top == 0x9000);
    assert(!kthread_stack_allocate(&thread) && allocations == 1);
    kthread_stack_release(&thread);
    assert(!thread.stack_phys && !thread.stack_top && releases == 1);
    kthread_stack_release(&thread);
    kthread_stack_release(NULL);
    assert(releases == 1);
    return 0;
}
