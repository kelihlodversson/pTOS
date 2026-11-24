#include "emutos.h"
#include "gemerror.h"
#include "process_observer.h"

LONG process_observer_attach(KPROC *target, PROCESS_OBSERVER **out)
{
    KPROC *retained;
    if (!out || *out) return EINVAL;
    retained = kproc_retain_live(target);
    if (!retained) return EACCDN;
    *out = retained;
    return E_OK;
}
BOOL process_observer_snapshot(const PROCESS_OBSERVER *observer, PROCESS_COMPLETION *out)
{
    return kproc_ref_completion(observer, out);
}
BOOL process_observer_ready(void *observer)
{
    return kproc_ref_terminal(observer); /* No owner lock, allocation or copyout. */
}
void process_observer_release(PROCESS_OBSERVER **handle)
{
    PROCESS_OBSERVER *observer;
    if (!handle || !*handle) return;
    observer = *handle;
    *handle = NULL;
    kproc_release(observer);
}
