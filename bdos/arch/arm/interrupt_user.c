/* Capture only: no detached exception continuation and no scheduler transfer. */
#include "emutos.h"
#if CONF_WITH_NATIVE_SCHED && CONF_WITH_USER_ASPACE
#include "kthread_context.h"
#include "kaspace.h"
#include "aspace32.h"
#include "string.h"
#include "biosext.h"
extern PD *run;
#if CONF_WITH_EXIT_HANDOFF_TEST
static ULONG irq_pattern_seen;
static BOOL irq_pattern_bad, irq_pattern_reported;
static void check_irq_pattern(const ARM_USER_IRQ_FRAME *f)
{
    UWORD i;
    if (f->r[12]!=0x71307132UL) return; /* installed last, invalidated first */
    irq_pattern_seen++;
    for (i=0;i<12;i++)
        if (i==1 ? f->r[i]>4000000UL : f->r[i]!=0x71307130UL) irq_pattern_bad=TRUE;
    if (f->user_lr!=0x7130713eUL) irq_pattern_bad=TRUE;
#if FPU_ALL_BYTES == 264
    if (*(const ULONG *)f->vfp!=0x00400000UL) irq_pattern_bad=TRUE;
    for (i=8;i<FPU_ALL_BYTES;i+=4)
        if (*(const ULONG *)(f->vfp+i)!=0x71307130UL) irq_pattern_bad=TRUE;
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
}
#endif
void arm_capture_user_irq(const ARM_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    ARM_USER_IRQ_CAPTURE *capture;
    ARM_IRQ_BINDING binding;
    ULONG tid;
    if ((frame->cpsr&0x1f)!=0x10) return; /* Never interpret a kernel IRQ as user. */
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution) return; /* Includes nested different-PROCESS launchers. */
    binding.root=aspace_hw_current();
    binding.expected_root=aspace_root(kaspace_backend(space));
    __asm__ volatile ("mrc p15,0,%0,c1,c0,0" : "=r"(binding.sctlr));
    __asm__ volatile ("mrc p15,0,%0,c2,c0,2" : "=r"(binding.ttbcr));
    __asm__ volatile ("mrc p15,0,%0,c3,c0,0" : "=r"(binding.dacr));
    __asm__ volatile ("mrc p15,0,%0,c13,c0,1" : "=r"(binding.context_id));
#ifdef __ARM_PCS_VFP
    __asm__ volatile ("vmrs %0,fpexc" : "=r"(binding.fpexc));
#else
    binding.fpexc=0;
#endif
    binding.frame_address=(uintptr_t)frame;
    if (!arm_process_irq_stack(run,&binding.stack_base,&binding.stack_top)) return;
    capture=&execution->interrupted_user;
    capture->valid=FALSE;
    if (!arm_irq_user_valid(frame,&binding,USER_VA_LIMIT) || capture->sequence==~(UQUAD)0) return;
    memcpy(&capture->image,frame,sizeof(*frame));
    capture->binding=binding;
    capture->execution=execution->identity;
    capture->tid=tid; capture->cpu=execution->owner_cpu;
    capture->sequence++;
    capture->valid=TRUE;
#if CONF_WITH_EXIT_HANDOFF_TEST
    check_irq_pattern(&capture->image);
#endif
}
#endif
