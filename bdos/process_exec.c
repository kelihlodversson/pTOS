/* Owned launch/observation rights, not a PROCESS completion registry. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "ksched.h"
#include "fs.h"
#include "proc.h"
#include "process_exec_internal.h"

struct process_exec_handle {
    struct process_exec_handle *next;
    ULONG number;
    BOOL committing;
    void *prepared;
    PROCESS_OBSERVER *observer;
};
static ULONG last_handle; /* Positive scalar ABI; never wrap or reuse. */
extern PD *run;

static BOOL current_owner(KPROC *owner)
{
    return owner && kproc_live_pd(owner) == run;
}
static struct process_exec_handle **find_handle(KPROC *owner, ULONG number)
{
    struct process_exec_handle **link = kproc_exec_handles(owner);
    if (!number || !link) return NULL;
    while (*link && (*link)->number != number) link = &(*link)->next;
    return *link ? link : NULL;
}
static void dispose_handle(struct process_exec_handle *handle)
{
    if (handle->prepared) ksched_process_cancel(&handle->prepared);
    process_observer_release(&handle->observer);
    kfree(handle);
}
LONG proc_exec_handle_prepare(KPROC *owner, char *path, char *tail, char *environment, char *directory, ULONG policy)
{
    struct process_exec_handle *handle;
    struct process_exec_handle **head;
    LONG result;
    if (!current_owner(owner)) return EACCDN;
    if (policy > PROCESS_EXEC_DEFER_REPLACEMENT) return EINVAL;
    if (last_handle == 0x7fffffffUL) return EAGAIN;
    head = kproc_exec_handles(owner);
    if (!head) return EACCDN;
    handle = kalloc(sizeof(*handle));
    if (!handle) return ENSMEM;
    handle->next = NULL;
    handle->prepared = NULL;
    handle->observer = NULL;
    handle->committing = FALSE;
    /* Burn the identity before loader callbacks can reenter preparation. A
     * failed preparation may leave a hole, but can never recycle a token. */
    handle->number = ++last_handle;
    /* The synchronous loader acquires every child resource and observer before
     * returning a private reservation. Failed construction transfers nothing. */
    result = proc_exec_prepare(path, tail, environment, directory, policy,
                               &handle->observer, &handle->prepared);
    if (result != E_OK) { kfree(handle); return result; }
    handle->next = *head;
    *head = handle;
    return (LONG)handle->number;
}
LONG proc_exec_handle_commit(KPROC *owner, ULONG number)
{
    struct process_exec_handle **link, *handle;
    LONG result;
    if (!current_owner(owner)) return EACCDN;
    link = find_handle(owner, number);
    if (!link) return EIHNDL;
    handle = *link;
    if (!handle->prepared || handle->committing) return EACCDN;
    /* Keep a stable record across callbacks which can insert other rights.
     * CLOSE and late prepared-lease attachment reject this in-flight record. */
    handle->committing = TRUE;
    result = ksched_process_commit(&handle->prepared);
    handle->committing = FALSE;
    return result;
}

LONG proc_exec_handle_snapshot(KPROC *owner, ULONG number, PROCESS_COMPLETION *out)
{
    struct process_exec_handle **link;
    if (!out) return EINVAL;
    if (!current_owner(owner)) return EACCDN;
    link = find_handle(owner, number);
    if (!link) return EIHNDL;
    return process_observer_snapshot((*link)->observer, out) ? E_OK : EIMBA;
}
LONG proc_exec_handle_close(KPROC *owner, ULONG number)
{
    struct process_exec_handle **link, *handle;
    if (!current_owner(owner)) return EACCDN;
    link = find_handle(owner, number);
    if (!link) return EIHNDL;
    handle = *link;
    if (handle->committing) return EACCDN;
    *link = handle->next; /* Reentrant cleanup cannot close it a second time. */
    dispose_handle(handle);
    return E_OK;
}
LONG proc_exec_retain_observer(KPROC *owner, ULONG number, BOOL require_prepared, PROCESS_OBSERVER **out)
{
    struct process_exec_handle **link;
    KPROC *retained;
    if (!out || *out) return EINVAL;
    if (!current_owner(owner)) return EACCDN;
    link = find_handle(owner, number);
    if (!link) return EIHNDL;
    if (require_prepared && (!(*link)->prepared || (*link)->committing)) return EACCDN;
    /* Existing observation may already refer to retired canonical metadata.
     * A lease must retain it, not attempt a new live-target attachment. */
    retained = kproc_retain_identity((*link)->observer);
    if (!retained) return EAGAIN;
    *out = retained;
    return E_OK;
}
void proc_exec_owner_exit(KPROC *owner)
{
    struct process_exec_handle **head = kproc_exec_handles(owner);
    if (!head) return;
    while (*head) {
        struct process_exec_handle *handle = *head;
        *head = handle->next;
        dispose_handle(handle);
    }
}
