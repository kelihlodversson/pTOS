/* Diagnostic capture and the restricted default-off outer USER IRQ tail. */
#include "emutos.h"
#if CONF_WITH_NATIVE_SCHED && CONF_WITH_USER_ASPACE
#include "kthread_context.h"
#include "ksched.h"
#include "kaspace.h"
#include "aspace32.h"
#include "string.h"
#include "biosext.h"
extern PD *run;
#if CONF_WITH_EXIT_HANDOFF_TEST
static ULONG irq_pattern_seen, irq_roundtrip_seen;
static BOOL irq_pattern_bad, irq_pattern_reported;
static void check_irq_pattern(const ARM_USER_IRQ_FRAME *f)
{
    UWORD i;
    if (f->r[12]!=0x71307132UL) return; /* installed last, invalidated first */
    irq_pattern_seen++;
    for (i=0;i<12;i++)
#if CONF_WITH_IRQ_RESTART
        if (i==1 ? f->r[i]>40000000UL : f->r[i]!=0x71307130UL) irq_pattern_bad=TRUE;
#else
        if (i==1 ? f->r[i]>4000000UL : f->r[i]!=0x71307130UL) irq_pattern_bad=TRUE;
#endif
    if (f->user_lr!=0x7130713eUL) irq_pattern_bad=TRUE;
#if FPU_ALL_BYTES == 264
    if (f->vfp[0]!=0x00400000UL) irq_pattern_bad=TRUE;
    for (i=2;i<FPU_ALL_BYTES/4;i++)
        if (f->vfp[i]!=0x71307130UL) irq_pattern_bad=TRUE;
#else
    irq_pattern_bad=TRUE; /* The native pattern probe targets Cortex-A7 D32. */
#endif
}
void arm_irq_capture_report(void)
{
    if (irq_pattern_bad) panic("ARM interrupted-user register capture mismatch");
    if (!irq_pattern_seen || irq_pattern_reported) return;
    irq_pattern_reported=TRUE;
    kprintf("native ARM interrupted-user integer/VFP/banked capture: PASS\r\n");
    if (!irq_roundtrip_seen) panic("ARM owned IRQ round trip not exercised");
    kprintf("native ARM fresh owned IRQ image round trip: PASS\r\n");
}
#endif
static BOOL read_binding(KASPACE *space,const ARM_USER_IRQ_FRAME *frame,ARM_IRQ_BINDING *b)
{
    b->root=aspace_hw_current();
    b->expected_root=aspace_root(kaspace_backend(space));
    __asm__ volatile ("mrc p15,0,%0,c1,c0,0" : "=r"(b->sctlr));
    __asm__ volatile ("mrc p15,0,%0,c2,c0,2" : "=r"(b->ttbcr));
    __asm__ volatile ("mrc p15,0,%0,c3,c0,0" : "=r"(b->dacr));
    __asm__ volatile ("mrc p15,0,%0,c13,c0,1" : "=r"(b->context_id));
    __asm__ volatile ("mrc p15,0,%0,c2,c0,0" : "=r"(b->ttbr0));
    __asm__ volatile ("mrc p15,0,%0,c2,c0,1" : "=r"(b->ttbr1));
    __asm__ volatile ("mrc p15,0,%0,c1,c0,2" : "=r"(b->cpacr));
#ifdef __ARM_PCS_VFP
    __asm__ volatile ("vmrs %0,fpexc" : "=r"(b->fpexc));
#else
    b->fpexc=0;
#endif
    b->frame_address=(uintptr_t)frame;
    return arm_process_irq_stack(run,&b->stack_base,&b->stack_top);
}
void arm_capture_user_irq(const ARM_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    ARM_USER_IRQ_CAPTURE *capture;
    ARM_IRQ_BINDING binding;
    ULONG tid;
    /* Wake metadata only; no kernel-frame capture or scheduler transfer. IRQ
     * readiness is durable before this handler returns on the original frame. */
    ksched_irq_notify();
    if ((frame->cpsr&0x1f)!=0x10) return; /* Never interpret a kernel IRQ as user. */
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution) return; /* Includes nested different-PROCESS launchers. */
    if (!read_binding(space,frame,&binding)) return;
    capture=&execution->interrupted_user;
    capture->valid=FALSE;
    if (!arm_irq_user_valid(frame,&binding,USER_VA_LIMIT) || capture->sequence==~(UQUAD)0) return;
    memcpy(&capture->image,frame,sizeof(*frame));
    capture->binding=binding;
    capture->execution=execution->identity;
    capture->tid=tid; capture->cpu=execution->owner_cpu;
    capture->sequence++;
    capture->valid=TRUE;
#if CONF_WITH_IRQ_RESTART
    interrupt_restart_user_capture(execution);
#endif
#if CONF_WITH_EXIT_HANDOFF_TEST
    if (!arm_irq_return_controls_valid(&capture->image,&binding)) return;
    check_irq_pattern(&capture->image);
    if (capture->image.r[12]==0x71307132UL) {
        ARM_USER_IRQ_FRAME *destination=(ARM_USER_IRQ_FRAME *)(uintptr_t)frame;
        UWORD i;
        if (!arm_irq_same_entry_valid(capture,frame,&binding,execution->identity,
                capture->sequence,tid,execution->owner_cpu,USER_VA_LIMIT))
            panic("ARM copied IRQ image ownership mismatch");
        /* Proof is deliberately confined to this entry, before the handler.
         * Poison integer/VFP saved bytes; real POP/RFE must consume the copy. */
        *(volatile ULONG *)&destination->r[0]=0;
#if FPU_ALL_BYTES == 264
        for(i=2;i<FPU_ALL_BYTES/4;i++) ((volatile ULONG *)destination->vfp)[i]=0;
#else
        (void)i;
#endif
        __asm__ volatile ("" ::: "memory");
        memcpy(destination,&capture->image,sizeof(*destination));
        irq_roundtrip_seen++;
    }
#endif
}
#if CONF_WITH_IRQ_RESTART
void arm_restart_user_irq(ARM_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    ULONG tid;
    ARM_USER_IRQ_CAPTURE *c;
    ARM_IRQ_BINDING b;
    if ((frame->cpsr&0x1f)!=0x10) return;
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution || execution->irq_restart.state!=IRQ_RESTART_ARMED) return;
    c=&execution->interrupted_user;
    if (!read_binding(space,frame,&b) ||
        !arm_irq_same_entry_valid(c,frame,&b,execution->identity,c->sequence,tid,
                                 execution->owner_cpu,USER_VA_LIMIT) ||
        !interrupt_restart_admit(execution,c->sequence,frame->pc,frame->user_sp)) return;
    (void)interrupt_restart_suspend(execution);
    if (!interrupt_restart_return_valid(execution) || !read_binding(space,frame,&b) ||
        memcmp(&b,&c->binding,sizeof(b)) || !arm_irq_user_valid(&c->image,&b,USER_VA_LIMIT) ||
        !arm_irq_return_controls_valid(&c->image,&b) || memcmp(frame,&c->image,sizeof(*frame)))
        panic("IRQ restart: ARM retained return binding changed");
#if CONF_WITH_IRQ_RESTART_TEST
    *(volatile ULONG *)&frame->r[0]=0;
#if FPU_ALL_BYTES
    memset(frame->vfp,0,sizeof(frame->vfp));
#endif
#endif
    memcpy(frame,&c->image,sizeof(*frame));
    interrupt_restart_ack(execution);
}
#endif

#endif
