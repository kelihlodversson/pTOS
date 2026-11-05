/* Initial non-runnable thread identity; scheduling remains in native_sched. */
#include "emutos.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "kthread.h"
#include "fs.h"
#include "proc.h"

struct kthread {
    ULONG tid;
    KPROC *process;             /* owning identity reference */
    KASPACE *space;             /* owning mapping reference */
    KTHREAD *process_next;      /* borrowed process membership */
};

/* Never reuse a TID during a boot. Exhaustion fails closed. */
static ULONG next_tid = 1;
#if CONF_WITH_X86_64_MEMTEST
static BOOL fail_after_attach;
void kthread_test_fail_after_attach(void) { fail_after_attach = TRUE; }
#endif

KTHREAD *kthread_create(PD *pd)
{
    KTHREAD *thread;

    if (!next_tid)
        return NULL;
    thread = kalloc(sizeof(*thread));
    if (!thread)
        return NULL;
    if (!kproc_thread_attach(pd, thread, &thread->process, &thread->space,
                             &thread->process_next)) {
        kfree(thread);
        return NULL;
    }
#if CONF_WITH_X86_64_MEMTEST
    if (fail_after_attach) {
        fail_after_attach = FALSE;
        kproc_thread_detach(thread->process, thread, thread->process_next);
        kaspace_put(thread->space);
        kproc_release(thread->process);
        kfree(thread);
        return NULL;
    }
#endif
    thread->tid = next_tid++;
    return thread;
}

void kthread_destroy(KTHREAD *thread)
{
    PD *dispose;
    if (!thread)
        return;
    dispose = kproc_thread_detach(thread->process, thread, thread->process_next);
    kaspace_put(thread->space);
    kproc_release(thread->process);
    kfree(thread);
    if (dispose)
        proc_dispose_retained(dispose);
}

ULONG kthread_tid(const KTHREAD *thread) { return thread ? thread->tid : 0; }
KPROC *kthread_process(const KTHREAD *thread) { return thread ? thread->process : NULL; }
KASPACE *kthread_aspace(const KTHREAD *thread) { return thread ? thread->space : NULL; }

/* Used only to walk the process's non-owning membership list. */
KTHREAD **kthread_next_link(KTHREAD *thread) { return &thread->process_next; }
