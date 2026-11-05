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
static ULONG last_tid;
#if CONF_WITH_X86_64_MEMTEST
static BOOL fail_after_attach;
void kthread_test_fail_after_attach(void) { fail_after_attach = TRUE; }
ULONG kthread_test_set_last_tid(ULONG tid)
{
    ULONG previous = last_tid;
    last_tid = tid;
    return previous;
}
#endif

KTHREAD *kthread_create(PD *pd)
{
    KTHREAD *thread;

    if (last_tid == ~(ULONG)0)
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
    thread->tid = ++last_tid;
    return thread;
}

void kthread_destroy(KTHREAD *thread)
{
    BOOL dispose;
    KPROC *process;
    if (!thread)
        return;
    process = thread->process;
    dispose = kproc_thread_detach(process, thread, thread->process_next);
    kaspace_put(thread->space);
    kfree(thread);
    if (dispose)
        proc_dispose_process(process);
    kproc_release(process);
}

ULONG kthread_tid(const KTHREAD *thread) { return thread ? thread->tid : 0; }
KPROC *kthread_process(const KTHREAD *thread) { return thread ? thread->process : NULL; }
KASPACE *kthread_aspace(const KTHREAD *thread) { return thread ? thread->space : NULL; }

/* Used only to walk the process's non-owning membership list. */
KTHREAD **kthread_next_link(KTHREAD *thread) { return &thread->process_next; }
