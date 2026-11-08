/* Hardware format-0 user IRQ capture only. No saved-frame restore/dispatch. */
#include "emutos.h"
#if CONF_WITH_NATIVE_SCHED && CONF_WITH_USER_ASPACE
#include "kthread_context.h"
#include "kaspace.h"
#include "aspace32.h"
#include "../../../bios/processor.h"
#include "string.h"
#include "biosext.h"
extern PD *run;
#if CONF_WITH_EXIT_HANDOFF_TEST
/* Debugger-readable native evidence: seen, mismatch, nonzero FPIAR captures.
 * Test-only, serialized under the entry wrapper's local IRQ exclusion. */
ULONG m68k_irq_test_state[3];
#define irq_pattern_seen m68k_irq_test_state[0]
#define irq_pattern_bad m68k_irq_test_state[1]
static BOOL irq_pattern_reported;
static void check_irq_pattern(const M68K_USER_IRQ_CAPTURE *c)
{
    UWORD i;
    const M68K_USER_IRQ_FRAME *f=&c->image;
    if (f->d[7]!=0x71307137UL) return;
    irq_pattern_seen++;
    if (f->d[0]>4000000UL) irq_pattern_bad=TRUE;
    for (i=1;i<7;i++) if (f->d[i]!=0x71307130UL) irq_pattern_bad=TRUE;
    for (i=0;i<6;i++) if (f->a[i]!=0x71307130UL) irq_pattern_bad=TRUE;
    if (c->binding.fpu_type) {
        if (c->fpu.ctl[2]!=f->a[6]) irq_pattern_bad=TRUE;
        if (c->fpu.ctl[2]) m68k_irq_test_state[2]++;
        for (i=1;i<8;i++)
            if (memcmp(c->fpu.regs,c->fpu.regs+12*i,12)) irq_pattern_bad=TRUE;
    }
}
void m68k_irq_capture_report(void)
{
    if (irq_pattern_bad) panic("m68k interrupted-user register capture mismatch");
    if (!irq_pattern_seen || irq_pattern_reported) return;
    irq_pattern_reported=TRUE;
    kprintf("native m68k interrupted-user GPR/USP/FPU format-0 capture: PASS\r\n");
}
#endif
void m68k_capture_user_irq(const M68K_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    M68K_USER_IRQ_CAPTURE *capture;
    M68K_IRQ_BINDING binding;
    ULONG tid;
    /* Inspect only the header until exact model/format admission. The format
     * word exists on every supported MMU CPU. Synthetic timer frames are S=1. */
    if ((frame->sr&0x2000) || (frame->format_vector&0xf000)) return;
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution) return;
    binding.root=aspace_hw_current();
    binding.expected_root=aspace_root(kaspace_backend(space));
    binding.model=mcpu; binding.fpu_type=fputype;
    binding.frame_address=(uintptr_t)frame;
    if (!m68k_process_irq_stack(run,&binding.stack_base,&binding.stack_top)) return;
    capture=&execution->interrupted_user;
    capture->valid=FALSE;
    if (!m68k_irq_user_valid(frame,&binding,USER_VA_LIMIT) || capture->sequence==~(UQUAD)0) return;
    memcpy(&capture->image,frame,sizeof(*frame));
    capture->binding=binding;
    memset(&capture->fpu,0,sizeof(capture->fpu));
    if (fputype) {
        /* Reuse the existing 030/040/060 backend and restore immediately.
         * FSAVE images remain opaque and model-tagged, not a new restore ABI. */
        m68k_fpu_save(&capture->fpu);
        m68k_fpu_restore(&capture->fpu);
    }
    capture->execution=execution->identity;
    capture->tid=tid; capture->cpu=execution->owner_cpu;
    capture->sequence++;
    capture->valid=TRUE;
#if CONF_WITH_EXIT_HANDOFF_TEST
    check_irq_pattern(capture);
#endif
}
#endif
