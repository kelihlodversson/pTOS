/* One retained outer USER-return operation per existing execution context.
 * Test preparation and automatic timer admission share the same transaction. */
#include "emutos.h"
#if CONF_WITH_IRQ_RESTART
#include "interrupt_restart.h"
#include "kthread_context.h"
#include "kthread.h"
#include "kproc.h"
#include "kaspace.h"
#include "ksched.h"
#include "fs.h"
#include "proc.h"
#include "gemerror.h"
#include "biosext.h"
#include "string.h"
#if CONF_WITH_NATIVE_PREEMPT && CONF_WITH_PRIVATE_CLONE_TEST
#include "tosvars.h"
typedef struct { ULONG start,ticks; } PREEMPT_TEST_DELAY;
static BOOL preempt_test_ready(void *argument)
{
    PREEMPT_TEST_DELAY *delay=argument;
    return (ULONG)((ULONG)hz_200-delay->start)>=delay->ticks;
}
LONG interrupt_preempt_test_delay(ULONG ticks)
{
    PREEMPT_TEST_DELAY delay;
    if (!ticks || ticks>20) return EINVAL;
    delay.start=(ULONG)hz_200;delay.ticks=ticks;
    return ksched_wait_cancelable(preempt_test_ready,&delay)==KSCHED_WAIT_READY?E_OK:EACCDN;
}
#endif
extern PD *run;

static BOOL owner_valid(KTHREAD_CONTEXT *c)
{
    IRQ_RESTART *r=&c->irq_restart;
    KASPACE *space=NULL;
    ULONG tid=0;
    KTHREAD_CONTEXT *current=ksched_interrupt_target(run,&space,&tid);
    return current==c && c->on_cpu && c->identity==r->execution && c->owner_cpu==r->cpu &&
        tid==r->tid && r->thread==ksched_exit_thread(run) && kthread_is_executing(r->thread) &&
        r->process==kthread_process(r->thread) && kproc_ref_number(r->process)==r->process_number &&
        kproc_live_pd(r->process)==run && kproc_active_space(r->process)==r->space &&
        space==r->space;
}
static BOOL resident(KASPACE *space, VADDR first, VADDR bytes, UWORD access)
{
    VADDR page,last;
    ASPACE *as=kaspace_backend(space);
    if (!bytes || first>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-first) return FALSE;
    last=(first+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    for(page=first&~(VADDR)(USER_PAGE_SIZE-1);;page+=USER_PAGE_SIZE) {
        UWORD rights=0;
        if (!aspace_translate(as,page,NULL,&rights) || (rights&access)!=access) return FALSE;
        if(page==last) return TRUE;
    }
}
static BOOL holds_valid(const IRQ_RESTART *r)
{
    return kaspace_lease_valid(r->code) && kaspace_stack_hold_space(r->stack)==r->space &&
        resident(r->space,r->code_address,r->code_bytes,ASPACE_PROT_USER|ASPACE_PROT_EXEC) &&
        resident(r->space,r->stack_address,r->stack_bytes,
                 ASPACE_PROT_USER|(r->automatic?0:ASPACE_PROT_WRITE));
}
static void release(IRQ_RESTART *r)
{
    KASPACE_LEASE *code=r->code,*stack=r->stack;
    /* Detach before either put can run an ordinary safe-context destructor. */
    memset(r,0,sizeof(*r));
    kaspace_lease_release(&stack);
    kaspace_lease_release(&code);
}
void interrupt_restart_detach(KTHREAD_CONTEXT *c,IRQ_RESTART *retired)
{
    IRQ_RESTART *r=&c->irq_restart;
    if (r->state==IRQ_RESTART_FROZEN || r->state==IRQ_RESTART_SUSPENDED ||
        r->state==IRQ_RESTART_RETURNING)
        panic("IRQ restart: retiring an unacknowledged continuation");
    *retired=*r;
    memset(r,0,sizeof(*r));
}
void interrupt_restart_retire(IRQ_RESTART *retired) { release(retired); }
void interrupt_restart_dispose(KTHREAD_CONTEXT *c)
{
    IRQ_RESTART retired;
    interrupt_restart_detach(c,&retired);
    interrupt_restart_retire(&retired);
}
#if CONF_WITH_NATIVE_PREEMPT
LONG interrupt_restart_reserve(IRQ_RESTART *r)
{
    LONG error;
    if (r->state!=IRQ_RESTART_EMPTY || r->code || r->stack) return EACCDN;
    error=kaspace_irq_reserve(&r->code);
    if (!error) error=kaspace_irq_reserve(&r->stack);
    if (error) { release(r);return error; }
    r->automatic=TRUE;r->state=IRQ_RESTART_ARMED;
    return E_OK;
}
static BOOL automatic_hold(KTHREAD_CONTEXT *c,VADDR pc,VADDR sp)
{
    IRQ_RESTART *r=&c->irq_restart;
    KASPACE *space=NULL;
    ULONG tid=0;
    KTHREAD *thread;
    VADDR stack_view=sp-1;
    if (!ksched_preempt_due() || !pc || !sp ||
        ksched_interrupt_target(run,&space,&tid)!=c) return FALSE;
    thread=ksched_exit_thread(run);
    if (!r->execution) {
        r->execution=c->identity;r->tid=tid;r->cpu=c->owner_cpu;
        r->thread=thread;r->process=kthread_process(thread);r->space=space;
        r->process_number=kproc_ref_number(r->process);
    }
    if (!owner_valid(c)) return FALSE;
    if (!kaspace_irq_activate(r->code,space,pc,ASPACE_PROT_USER|ASPACE_PROT_EXEC)) return FALSE;
    r->reap_pending=TRUE; /* retargeting may unblock an older source pin */
    if (!kaspace_irq_activate(r->stack,space,stack_view,ASPACE_PROT_USER|ASPACE_PROT_WRITE)) {
        /* A page-aligned USER SP can be the bottom of its resident page,
         * with a guard below. Prefer the existing exclusive-top view, but
         * admit that resident page when the preceding page has no authority.
         * Both attempts use the same preallocated lease and exact rights. */
        if ((sp&(USER_PAGE_SIZE-1)) ||
            !kaspace_irq_activate(r->stack,space,sp,ASPACE_PROT_USER|ASPACE_PROT_WRITE)) return FALSE;
        stack_view=sp;
    }
    r->code_address=pc&~(VADDR)(USER_PAGE_SIZE-1);r->code_bytes=USER_PAGE_SIZE;
    r->stack_address=stack_view&~(VADDR)(USER_PAGE_SIZE-1);r->stack_bytes=USER_PAGE_SIZE;
    return TRUE;
}
void interrupt_restart_reap(KTHREAD_CONTEXT *c)
{
    IRQ_RESTART *r=&c->irq_restart;
    if(!r->reap_pending)return;
    if(c->on_cpu)panic("IRQ retirement on current execution");
    r->reap_pending=FALSE;
    kaspace_reap_deferred(r->space);
}
#endif
#if CONF_WITH_IRQ_RESTART_TEST
static LONG prepare(KTHREAD_CONTEXT *c, KASPACE *space, ULONG tid,
                    const IRQ_RESTART_ARGS *a, UWORD fail)
{
    IRQ_RESTART draft;
    KTHREAD *thread=ksched_exit_thread(run);
    LONG error;
    if (!thread || c->irq_restart.state!=IRQ_RESTART_EMPTY ||
        ksched_cancellation_pending()) return EACCDN;
    if (!a->code_bytes || !a->stack_bytes || a->code_bytes>65536 || a->stack_bytes>65536 ||
        a->code>=USER_VA_LIMIT || a->stack>=USER_VA_LIMIT ||
        a->code_bytes>USER_VA_LIMIT-a->code || a->stack_bytes>USER_VA_LIMIT-a->stack) return EINVAL;
    if (!resident(space,(VADDR)a->code,(VADDR)a->code_bytes,ASPACE_PROT_USER|ASPACE_PROT_EXEC) ||
        !resident(space,(VADDR)a->stack,(VADDR)a->stack_bytes,ASPACE_PROT_USER|ASPACE_PROT_WRITE)) return EACCDN;
    memset(&draft,0,sizeof(draft));
    draft.space=space; draft.thread=thread; draft.process=kthread_process(thread);
    draft.process_number=kproc_ref_number(draft.process);
    draft.execution=c->identity; draft.tid=tid; draft.cpu=c->owner_cpu;
    draft.code_address=(VADDR)a->code; draft.code_bytes=(VADDR)a->code_bytes;
    draft.stack_address=(VADDR)a->stack; draft.stack_bytes=(VADDR)a->stack_bytes;
    draft.capture_only=fail==3;
    draft.suppress_credit=fail==4;
    error=kaspace_lease_acquire(space,draft.code_address,draft.code_bytes,
                               ASPACE_PROT_USER|ASPACE_PROT_EXEC,&draft.code);
    if (error) goto failed;
    if (!kaspace_lease_hold_private(draft.code)) { error=EACCDN;goto failed; }
    if (fail==1) { error=ENSMEM;goto failed; }
    error=kaspace_stack_hold_acquire(space,draft.stack_address,draft.stack_bytes,&draft.stack);
    if (error) goto failed;
    if (fail==2) { error=ENSMEM;goto failed; }
    /* All fallible preparation precedes publication. No IRQ has permission to
     * capture this operation until the single final state store. */
    {
        uintptr_t flags=native_sched_irq_save();
        KASPACE *current_space=NULL;
        ULONG current_tid=0;
        if (ksched_interrupt_target(run,&current_space,&current_tid)!=c ||
            current_space!=space || current_tid!=tid || !holds_valid(&draft) ||
            kproc_live_pd(draft.process)!=run || ksched_cancellation_pending() ||
            c->irq_restart.state!=IRQ_RESTART_EMPTY) {
            native_sched_irq_restore(flags); error=EACCDN;goto failed;
        }
        c->irq_restart=draft; /* move both owned handles, never a second owner */
        memset(&draft,0,sizeof(draft));
        c->irq_restart.state=IRQ_RESTART_ARMED;
        native_sched_irq_restore(flags);
    }
    return E_OK;
failed:
    release(&draft);
    return error;
}
LONG interrupt_restart_test(UQUAD address, UQUAD selector)
{
    KASPACE *space=NULL;
    ULONG tid=0;
    KTHREAD_CONTEXT *c=ksched_interrupt_target(run,&space,&tid);
    IRQ_RESTART *r;
    /* The ordinary xclone cleanup scope protects prepare/finish. It must be
     * gone at the later USER IRQ boundary; never bypass or alter that scope. */
    if (!c) return EINVFN;
    r=&c->irq_restart;
    if (selector==IRQ_RESTART_QUERY)
        return !address && owner_valid(c) ? (LONG)r->state:EACCDN;
    if (selector==IRQ_RESTART_FINISH || selector==IRQ_RESTART_ABORT) {
        LONG result;
        if (address || (r->state!=IRQ_RESTART_ARMED && r->state!=IRQ_RESTART_ACKED)) return EACCDN;
        if (!owner_valid(c)) return EACCDN;
        result=selector==IRQ_RESTART_ABORT || (r->state==IRQ_RESTART_ACKED && r->peer_ran) ? E_OK:EAGAIN;
        if (selector==IRQ_RESTART_FINISH && r->suppress_credit && !r->transported)
            result=EINVFN; /* injected no-credit proof must actually hand off */
        if (selector==IRQ_RESTART_FINISH && r->capture_only && r->captured && r->state==IRQ_RESTART_ARMED)
            result=1; /* explicitly capture/ordinary return, never a B2 success */
        release(r);
        return result;
    }
    if (selector==IRQ_RESTART_BEGIN || selector==IRQ_RESTART_CAPTURE_ONLY || selector==IRQ_RESTART_NO_CREDIT ||
        selector==IRQ_RESTART_FAIL_CODE || selector==IRQ_RESTART_FAIL_STACK) {
        IRQ_RESTART_ARGS args;
        LONG error;
        if (!address || address>=USER_VA_LIMIT) return EIMBA;
        error=kproc_copy_from_user_checked(&args,(VADDR)address,sizeof(args));
        if(error) return error;
        return prepare(c,space,tid,&args,selector==IRQ_RESTART_FAIL_CODE?1:
                       selector==IRQ_RESTART_FAIL_STACK?2:selector==IRQ_RESTART_CAPTURE_ONLY?3:
                       selector==IRQ_RESTART_NO_CREDIT?4:0);
    }
    return EINVFN;
}
#endif
void interrupt_restart_user_capture(KTHREAD_CONTEXT *c)
{
    IRQ_RESTART *r=&c->irq_restart;
    VADDR pc,sp;
    /* Architecture caller has already validated a genuine hardware USER IRQ.
     * Root/constructor selection is deliberately not user-execution evidence. */
    ksched_irq_restart_observe_user(c->identity);
#if ARCH_X86_64
    pc=(VADDR)c->interrupted_user.image.rip;sp=(VADDR)c->interrupted_user.image.rsp;
#elif ARCH_ARM
    pc=c->interrupted_user.image.pc;sp=c->interrupted_user.image.user_sp;
#else
    pc=c->interrupted_user.image.pc;sp=c->interrupted_user.image.usp;
#endif
    if(r->state==IRQ_RESTART_ARMED && pc>=r->code_address && pc-r->code_address<r->code_bytes &&
        sp>=r->stack_address && sp-r->stack_address<r->stack_bytes) r->captured=TRUE;
}
void interrupt_restart_user_entry(void)
{
    KASPACE *space=NULL;
    ULONG tid=0;
    uintptr_t flags=native_sched_irq_save();
    KTHREAD_CONTEXT *c=ksched_interrupt_target(run,&space,&tid);
#if CONF_WITH_NATIVE_PREEMPT
    BOOL reap=FALSE;
#endif
    if(c) ksched_irq_restart_observe_user(c->identity);
#if CONF_WITH_NATIVE_PREEMPT
    if(c && c->irq_restart.automatic && c->irq_restart.state==IRQ_RESTART_ARMED &&
       c->irq_restart.execution && !ksched_cleanup_active() && owner_valid(c)) {
        reap=kaspace_irq_park(c->irq_restart.stack);
        if(kaspace_irq_park(c->irq_restart.code))reap=TRUE;
        reap=reap || c->irq_restart.reap_pending;c->irq_restart.reap_pending=FALSE;
    }
#endif
    native_sched_irq_restore(flags);
#if CONF_WITH_NATIVE_PREEMPT
    if(reap) {
        /* This is an authenticated ordinary USER trap with its registers
         * saved, not an IRQ. Its live launcher/THREAD retains the root and
         * cleanup scope while the ordinary deferred reaper runs unlocked. */
        KSCHED_CLEANUP_SCOPE scope={0};
        ksched_cleanup_begin(&scope);kaspace_reap_deferred(space);ksched_cleanup_end(&scope);
    }
#endif
}
BOOL interrupt_restart_admit(KTHREAD_CONTEXT *c,UQUAD sequence,VADDR pc,VADDR sp)
{
    IRQ_RESTART *r=&c->irq_restart;
    if (r->state!=IRQ_RESTART_ARMED || r->capture_only || !sequence || ksched_cleanup_active()) return FALSE;
#if CONF_WITH_NATIVE_PREEMPT
    if (r->automatic) {
        if (!automatic_hold(c,pc,sp)) return FALSE;
    } else
#endif
    if (ksched_cancellation_pending() || !owner_valid(c) || !holds_valid(r) ||
        pc<r->code_address || pc-r->code_address>=r->code_bytes ||
        sp<r->stack_address || sp-r->stack_address>=r->stack_bytes) return FALSE;
    r->capture_sequence=sequence;
    r->selected=ksched_irq_restart_selected();
    r->user_observed=ksched_irq_restart_user_observed();
    r->state=IRQ_RESTART_FROZEN;
    return TRUE;
}
void interrupt_restart_root_ack(KTHREAD_CONTEXT *c)
{
    /* Called from verified root under leaf exclusion: only bounded metadata. */
    if (c->irq_restart.state==IRQ_RESTART_FROZEN) c->irq_restart.state=IRQ_RESTART_SUSPENDED;
}
static BOOL ready(void *argument) { (void)argument;return TRUE; }
BOOL interrupt_restart_suspend(KTHREAD_CONTEXT *c)
{
    IRQ_RESTART *r=&c->irq_restart;
    if (r->state!=IRQ_RESTART_FROZEN || !owner_valid(c) || ksched_cleanup_active()) panic("IRQ restart: invalid departure owner");
    /* No user wait, cancellation claim or predicate owner is introduced.
     * The retained exception stack is parked only after device work unwinds. */
    if (!ksched_block(ready,NULL)) panic("IRQ restart: refused prepared departure");
    if (r->state!=IRQ_RESTART_SUSPENDED || !owner_valid(c) || ksched_cleanup_active())
        panic("IRQ restart: restored owner identity changed");
#if CONF_WITH_PTERMGRP
    if (ksched_cancellation_pending()) {
        /* The existing cancellation protocol owns status and stopping. We are
         * back on A's verified launcher stack after root acknowledged departure;
         * leave all leases attached for its ordinary off-stack root reclaimer. */
        r->state=IRQ_RESTART_ABORTED;
        ksched_cancel_point();
        panic("IRQ restart: acknowledged cancellation returned");
    }
#endif
    if (!holds_valid(r)) {
        /* Keep owners attached until existing launcher/root stop acknowledgment.
         * Never put resources or fabricate completion on this exception stack. */
        r->state=IRQ_RESTART_ABORTED;
        ksched_fault(run,11);
        xterm(-1);
        panic("IRQ restart: mapping abort returned");
    }
    r->transported=TRUE;
    r->peer_ran=!r->suppress_credit && ksched_irq_restart_selected()>r->selected &&
        ksched_irq_restart_selected()-r->selected>=2 &&
        ksched_irq_restart_user_observed()>r->user_observed;
    /* Even an unproven handoff may have run a different kernel context.
     * Architectural restoration is mandatory independently of proof credit. */
    r->state=IRQ_RESTART_RETURNING;
    return r->peer_ran;
}
void interrupt_restart_ack(KTHREAD_CONTEXT *c)
{
    if(c->irq_restart.state!=IRQ_RESTART_RETURNING || !owner_valid(c) || ksched_cleanup_active())
        panic("IRQ restart: invalid architectural return acknowledgment");
#if CONF_WITH_NATIVE_PREEMPT
    if (c->irq_restart.automatic) {
#if CONF_WITH_PRIVATE_CLONE_TEST
        if (c->irq_restart.peer_ran) {
#if ARCH_X86_64
            UQUAD marker=c->interrupted_user.image.r15;
            if (marker==0x7130000000713015ULL) ksched_preempt_probe_return(1);
            if (marker==0x7240000000724015ULL) ksched_preempt_probe_return(2);
#elif ARCH_ARM
            ULONG marker=c->interrupted_user.image.r[12];
            if (marker==0x71307132UL) ksched_preempt_probe_return(1);
            if (marker==0x72407242UL) ksched_preempt_probe_return(2);
#else
            ULONG marker=c->interrupted_user.image.d[7];
            if (marker==0x71307137UL) ksched_preempt_probe_return(1);
            if (marker==0x72407247UL) ksched_preempt_probe_return(2);
#endif
        }
#endif
        /* Keep resident execution authority across repeated USER IRQs, even
         * if its source owner retired while suspended. The next authenticated
         * kernel entry parks views; no cleanup runs on this IRQ epilogue. */
        c->irq_restart.state=IRQ_RESTART_ARMED;
        return;
    }
#endif
    c->irq_restart.state=c->irq_restart.peer_ran?IRQ_RESTART_ACKED:IRQ_RESTART_ARMED;
    /* Both mapping owners survive until the next ordinary finish/exit path. */
}
BOOL interrupt_restart_return_valid(KTHREAD_CONTEXT *c)
{
    IRQ_RESTART *r=&c->irq_restart;
    return r->state==IRQ_RESTART_RETURNING && owner_valid(c) && !ksched_cleanup_active() && holds_valid(r) &&
        !c->interrupted_user.valid && c->interrupted_user.sequence==r->capture_sequence &&
        c->interrupted_user.execution==r->execution && c->interrupted_user.tid==r->tid &&
        c->interrupted_user.cpu==r->cpu;
}
#endif
