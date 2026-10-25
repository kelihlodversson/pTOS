/* Generic cooperative native scheduling; initially selected by CUIS. */
#ifndef KSCHED_H
#define KSCHED_H
#include "portab.h"
#include "bdosdefs.h"
#define KSCHED_LIMIT 8U
/* All programs must be prepared, unlaunched and distinct. Success consumes
 * them, writes their exit statuses, and returns after all tasks finish.
 * Failure before launch leaves every program with the caller. */
LONG ksched_run(PD * const *programs, UWORD count, LONG *results);
/* Bootstrap an unprepared ordinary synchronous launch. Allocations precede
 * image preparation. A retained PE_GO image keeps
 * its caller's ownership; other launches transfer ownership at user entry.
 * Success consumes the launch and returns its full synchronous exit status. */
LONG ksched_exec(PD *program, BOOL take_ownership, LONG *result);
BOOL ksched_active(void);
/* Called with IRQs masked. Predicates run with IRQs masked and must not
 * allocate, block or access user memory. FALSE means no managed task. */
BOOL ksched_block(BOOL (*ready)(void *), void *argument);
/* Standard async process calls are admitted only within a managed group.
 * Spawn consumes an unprepared, loaded basepage only on success. */
BOOL ksched_spawn_allowed(void);
LONG ksched_spawn(PD *pd);
LONG ksched_yield(void);
LONG ksched_wait(WORD pid, WORD options, UQUAD usage);
BOOL ksched_pid_reserved(WORD pid);
BOOL ksched_parent_pid(PD *pd, WORD *pid);
void ksched_exiting(PD *pd, UWORD status);
void ksched_fault(PD *pd, WORD signal);
LONG proc_prepare_scheduled(PD *pd);
LONG proc_start_scheduled(PD *pd, BOOL take_ownership);
#endif
