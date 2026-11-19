/* Restricted interrupted-user return transaction. No scheduling policy. */
#ifndef INTERRUPT_RESTART_H
#define INTERRUPT_RESTART_H
#include "portab.h"
#include <aspace_types.h>
#if CONF_WITH_IRQ_RESTART
struct kthread_context;
struct kaspace;
struct kaspace_lease;
struct kthread;
struct kproc;
typedef enum { IRQ_RESTART_EMPTY, IRQ_RESTART_ARMED, IRQ_RESTART_FROZEN,
               IRQ_RESTART_SUSPENDED, IRQ_RESTART_RETURNING, IRQ_RESTART_ACKED,
               IRQ_RESTART_ABORTED } IRQ_RESTART_STATE;
typedef struct {
    struct kaspace_lease *code, *stack; /* owning, acquired outside IRQ */
    struct kaspace *space; /* test leases or the existing automatic KTHREAD own it */
    struct kthread *thread; /* task owns EXECUTING THREAD through root acknowledgment */
    struct kproc *process; /* retained by that THREAD, never a reusable PD identity */
    UQUAD execution, process_number, capture_sequence, selected, user_observed;
    VADDR code_address, code_bytes, stack_address, stack_bytes;
    ULONG tid;
    UWORD cpu;
    IRQ_RESTART_STATE state;
    BOOL peer_ran, capture_only, captured, suppress_credit, transported;
    BOOL automatic, reap_pending; /* reusable timer views; safe-context cleanup */
} IRQ_RESTART;
#if CONF_WITH_NATIVE_PREEMPT
LONG interrupt_restart_reserve(IRQ_RESTART *);
void interrupt_restart_reap(struct kthread_context *); /* off-CPU root only */
void ksched_preempt_tick(void); /* 200 Hz IRQ: CPU metadata only */
BOOL ksched_preempt_due(void); /* masked authenticated outer USER IRQ */
#if CONF_WITH_PRIVATE_CLONE_TEST
void ksched_preempt_probe_return(UWORD pattern); /* bounded CPU-local proof stamp */
LONG interrupt_preempt_test_delay(ULONG ticks); /* regression transport only */
#define IRQ_PREEMPT_TEST_DELAY 0xc713c100UL
#endif
#endif
/* Test-only Pclone transport, not a public syscall/clone flag. */
#define IRQ_RESTART_BEGIN  0xc713b200UL
#define IRQ_RESTART_FINISH 0xc713b201UL
#define IRQ_RESTART_ABORT  0xc713b202UL
#define IRQ_RESTART_CAPTURE_ONLY 0xc713b203UL
#define IRQ_RESTART_QUERY 0xc713b204UL
#define IRQ_RESTART_NO_CREDIT 0xc713b205UL
#define IRQ_RESTART_FAIL_CODE  0xc713b211UL
#define IRQ_RESTART_FAIL_STACK 0xc713b212UL
typedef struct { UQUAD code, code_bytes, stack, stack_bytes; } IRQ_RESTART_ARGS;
LONG interrupt_restart_test(UQUAD address, UQUAD selector);
BOOL interrupt_restart_admit(struct kthread_context *, UQUAD sequence, VADDR pc, VADDR sp);
BOOL interrupt_restart_suspend(struct kthread_context *);
BOOL interrupt_restart_return_valid(struct kthread_context *);
void interrupt_restart_ack(struct kthread_context *);
void interrupt_restart_root_ack(struct kthread_context *); /* bounded leaf metadata only */
void interrupt_restart_dispose(struct kthread_context *); /* off-stack, outside leaf */
void interrupt_restart_detach(struct kthread_context *, IRQ_RESTART *); /* move only, no destruction */
void interrupt_restart_retire(IRQ_RESTART *); /* after replacement installation, outside leaf */
void interrupt_restart_user_capture(struct kthread_context *); /* genuine user IRQ only */
void interrupt_restart_user_entry(void); /* architecture-authenticated USER trap entry */
/* Boot-CPU metadata, not a second scheduler or remote-CPU synchronization. */
UQUAD ksched_irq_restart_selected(void);
UQUAD ksched_irq_restart_user_observed(void);
void ksched_irq_restart_observe_user(UQUAD execution);
#endif
#endif
