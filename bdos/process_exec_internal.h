/* PROCESS-owned launch rights; completion always remains canonical KPROC. */
#ifndef PROCESS_EXEC_INTERNAL_H
#define PROCESS_EXEC_INTERNAL_H
#include "process_observer.h"
#include "process_exec.h"
#if CONF_WITH_USER_ASPACE && CONF_WITH_NATIVE_SCHED
struct process_exec_handle;
/* Only the current live PROCESS may create/use rights. Owner retirement uses
 * its existing finalizer, never a GUI session, PID lookup, THREAD exit or reaper.
 * Trusted input strings remain valid for the synchronous preparation call. */
LONG proc_exec_handle_prepare(KPROC *owner, char *path, char *tail, char *environment, char *directory, ULONG policy);
LONG proc_exec_handle_commit(KPROC *owner, ULONG handle);
LONG proc_exec_handle_snapshot(KPROC *owner, ULONG handle, PROCESS_COMPLETION *out);
LONG proc_exec_handle_close(KPROC *owner, ULONG handle);
LONG proc_exec_retain_observer(KPROC *owner, ULONG handle, BOOL require_prepared, PROCESS_OBSERVER **out);
void proc_exec_owner_exit(KPROC *owner);
/* Request engine only; public dispatch remains disabled until all policies pass. */
LONG proc_exec_request(KPROC *owner, UQUAD address, UQUAD size);
#if CONF_WITH_PROCESS_EXEC_DISPATCH
LONG proc_exec_dispatch(long address, long size);
#endif
#endif
#endif
