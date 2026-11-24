/* Hardware format-0 user IRQ capture only. No saved-frame restore/dispatch. */
#include "emutos.h"
#if CONF_WITH_MULTITHREADING && CONF_WITH_USER_ASPACE
#include "kthread_context.h"
#include "ksched.h"
#include "kaspace.h"
#include "aspace32.h"
#include "../../../bios/processor.h"
#include "string.h"
#include "biosext.h"
#include "gemerror.h"
extern PD *run;
#if CONF_WITH_EXIT_HANDOFF_TEST
/* Debugger-readable evidence: seen, mismatch, nonzero FPIAR, owned copy-backs.
 * Test-only, serialized under the entry wrapper's local IRQ exclusion. These
 * observations include counters never read by C (FPIAR), so LTO must neither
 * discard their writes nor privatize the symbol the debugger resolves. */
volatile ULONG m68k_irq_test_state[4] __attribute__((used,externally_visible));
#define irq_pattern_seen m68k_irq_test_state[0]
#define irq_pattern_bad m68k_irq_test_state[1]
static BOOL irq_pattern_reported;
#define irq_roundtrip_seen m68k_irq_test_state[3]
static void check_irq_pattern(const M68K_USER_IRQ_CAPTURE *c)
{
    UWORD i;
    const M68K_USER_IRQ_FRAME *f=&c->image;
    if (f->d[7]!=0x71307137UL) return;
    irq_pattern_seen++;
#if CONF_WITH_IRQ_RESTART
    if (f->d[0]>40000000UL) irq_pattern_bad=TRUE;
#else
    if (f->d[0]>4000000UL) irq_pattern_bad=TRUE;
#endif
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
    if (irq_roundtrip_seen)
        kprintf("native m68k fresh owned format-0 IRQ image round trip: PASS\r\n");
}
#endif
/* Observe controls using the existing backends' exact CPU instructions. This
 * does not load a root or alter caches/MMU policy. 030 never executes MOVEC
 * for the 040-only control registers. */
static void read_control(M68K_IRQ_BINDING *b)
{
    memset(b->roots,0,sizeof(b->roots));
    memset(b->tt,0,sizeof(b->tt));
    if (b->model==30) {
        __asm__ volatile ("lea %0,%%a0; .long 0xf0104200" : "=m"(b->tc) :: "a0", "memory");
        __asm__ volatile ("lea %0,%%a0; .long 0xf0104e00" : "=m"(b->roots[0]) :: "a0", "memory");
        __asm__ volatile ("lea %0,%%a0; .long 0xf0104a00" : "=m"(b->roots[2]) :: "a0", "memory");
        __asm__ volatile ("lea %0,%%a0; .long 0xf0100a00" : "=m"(b->tt[0]) :: "a0", "memory");
        __asm__ volatile ("lea %0,%%a0; .long 0xf0100e00" : "=m"(b->tt[1]) :: "a0", "memory");
    } else {
        __asm__ volatile (".long 0x4e7a0003; move.l %%d0,%0" : "=m"(b->tc) :: "d0", "memory");
        __asm__ volatile (".long 0x4e7a0806; move.l %%d0,%0" : "=m"(b->roots[0]) :: "d0", "memory");
        __asm__ volatile (".long 0x4e7a0807; move.l %%d0,%0" : "=m"(b->roots[1]) :: "d0", "memory");
        __asm__ volatile (".long 0x4e7a0004; move.l %%d0,%0" : "=m"(b->tt[0]) :: "d0", "memory");
        __asm__ volatile (".long 0x4e7a0005; move.l %%d0,%0" : "=m"(b->tt[1]) :: "d0", "memory");
        __asm__ volatile (".long 0x4e7a0006; move.l %%d0,%0" : "=m"(b->tt[2]) :: "d0", "memory");
        __asm__ volatile (".long 0x4e7a0007; move.l %%d0,%0" : "=m"(b->tt[3]) :: "d0", "memory");
    }
}
#if CONF_WITH_SAVED_RETURN
extern LONG m68k_fpu_save_complete(void *);
extern void m68k_fpu_restore_complete(const void *);
#endif
#if CONF_WITH_IRQ_RESTART_TEST && CONF_M68K_ROM_WINDOW
static BOOL b2_capture_needed(const KTHREAD_CONTEXT *execution,const M68K_USER_IRQ_FRAME *frame,UWORD model)
{
    const IRQ_RESTART *r=&execution->irq_restart;
    if (r->state==IRQ_RESTART_ARMED && frame->pc>=r->code_address &&
        frame->pc-r->code_address<r->code_bytes && frame->usp>=r->stack_address &&
        frame->usp-r->stack_address<r->stack_bytes &&
        !(r->capture_only && r->captured) && !(r->suppress_credit && r->transported)) return TRUE;
#if CONF_WITH_EXIT_HANDOFF_TEST
    /* Preserve the independent capture and fresh copy-back witnesses. An
     * unarmed pattern needs only its first valid sample for each witness. */
    if (frame->d[7]==0x71307137UL && (!irq_pattern_seen ||
        (model==40 && !CONF_WITH_MFASTCALL && !irq_roundtrip_seen))) return TRUE;
#else
    UNUSED(model);
#endif
    return FALSE;
}
#endif
void m68k_capture_user_irq(const M68K_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    M68K_USER_IRQ_CAPTURE *capture;
    M68K_IRQ_BINDING binding;
    ULONG tid;
    /* Wake metadata only; no kernel-frame capture or scheduler transfer. IRQ
     * readiness is durable before this handler returns on the original frame. */
    ksched_irq_notify();
    /* Inspect only the header until exact model/format admission. The format
     * word exists on every supported MMU CPU. Synthetic timer frames are S=1. */
    if ((frame->sr&0x2000) || (frame->format_vector&0xf000)) return;
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution) return;
#if CONF_WITH_PREEMPTION
    /* Invalidate freshness even when skipping a diagnostic copy. The device
     * tick runs later; prepare on its last quantum tick, never afterward. */
    execution->interrupted_user.valid=FALSE;
    if (!ksched_preempt_capture_needed()) return;
#endif
    binding.root=aspace_hw_current();
    binding.expected_root=aspace_root(kaspace_backend(space));
    binding.model=mcpu; binding.fpu_type=fputype;
    if (!m68k_irq_fpu_supported(binding.model,binding.fpu_type)) return;
    binding.frame_address=(uintptr_t)frame;
    if (!m68k_process_irq_stack(run,&binding.stack_base,&binding.stack_top)) return;
    capture=&execution->interrupted_user;
    capture->valid=FALSE;
    if (!m68k_irq_user_valid(frame,&binding,USER_VA_LIMIT) || capture->sequence==~(UQUAD)0) return;
#if CONF_WITH_IRQ_RESTART_TEST && CONF_M68K_ROM_WINDOW
    /* A genuine peer USER IRQ is a witness even if there is no image to
     * retain. Complete FPU capture on every tick can overwhelm progress on
     * cycle-driven 040 emulation. Capture the active probe and standalone
     * diagnostic witnesses, then keep ordinary IRQ return. */
    ksched_irq_restart_observe_user(execution->identity);
    if (!b2_capture_needed(execution,frame,binding.model)) return;
#endif
    read_control(&binding);
    memcpy(&capture->image,frame,sizeof(*frame));
    capture->binding=binding;
    memset(&capture->fpu,0,sizeof(capture->fpu));
    capture->complete_fpu=!fputype;
    if (fputype) {
        /* Reuse the existing 030/040/060 backend and restore immediately.
         * FSAVE images remain opaque and model-tagged, not a new restore ABI. */
#if CONF_WITH_SAVED_RETURN
        if (mcpu==40) {
            if (m68k_fpu_save_complete(&capture->fpu)!=E_OK) return;
            m68k_fpu_restore_complete(&capture->fpu);
            capture->complete_fpu=TRUE;
        } else
#endif
        {
            m68k_fpu_save(&capture->fpu);
            m68k_fpu_restore(&capture->fpu);
        }
    }
    capture->execution=execution->identity;
    capture->tid=tid; capture->cpu=execution->owner_cpu;
    capture->sequence++;
    capture->valid=TRUE;
#if CONF_WITH_IRQ_RESTART
    interrupt_restart_user_capture(execution);
#endif
#if CONF_WITH_EXIT_HANDOFF_TEST
    check_irq_pattern(capture);
    if (capture->image.d[7]==0x71307137UL &&
        m68k_irq_same_entry_valid(capture,frame,&binding,execution->identity,
            capture->sequence,tid,execution->owner_cpu,USER_VA_LIMIT)) {
        M68K_USER_IRQ_FRAME *destination=(M68K_USER_IRQ_FRAME *)(uintptr_t)frame;
        *(volatile ULONG *)&destination->d[1]=0;
        __asm__ volatile ("" ::: "memory");
        memcpy(destination,&capture->image,sizeof(*destination));
        __asm__ volatile ("move.l %0,%%usp" :: "a"((ULONG)0) : "memory");
#if CONF_WITH_SAVED_RETURN
        if (fputype) {
            m68k_fpu_init();
            m68k_fpu_restore_complete(&capture->fpu);
        }
#endif
        irq_roundtrip_seen++;
    }
#endif
}
#if CONF_WITH_IRQ_RESTART
static BOOL restart_binding(KASPACE *space,M68K_USER_IRQ_FRAME *frame,M68K_IRQ_BINDING *b)
{
    b->model=mcpu;b->fpu_type=fputype;
    if (b->model!=40 || !m68k_irq_fpu_supported(b->model,b->fpu_type)) return FALSE;
    b->root=aspace_hw_current();b->expected_root=aspace_root(kaspace_backend(space));
    b->frame_address=(uintptr_t)frame;
    read_control(b);
    return m68k_process_irq_stack(run,&b->stack_base,&b->stack_top);
}
ULONG m68k_restart_user_irq(M68K_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    ULONG tid;
    M68K_USER_IRQ_CAPTURE *c;
    M68K_IRQ_BINDING b;
    if ((frame->sr&0x3000) || (frame->format_vector&0xf000)) return 0;
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution || execution->irq_restart.state!=IRQ_RESTART_ARMED) return 0;
#if CONF_WITH_PREEMPTION
    if (!ksched_preempt_due()) return 0;
#endif
    c=&execution->interrupted_user;
    if (!c->valid) return 0;
    /* This prefix was freshly reconstructed after device-register restore.
     * Compare it to the entry-owned copy; never dereference the discarded
     * entry prefix after a handler, nor recapture after the handoff. */
    if (!restart_binding(space,frame,&b) ||
        !m68k_irq_same_entry_valid(c,frame,&b,execution->identity,c->sequence,tid,
                                  execution->owner_cpu,USER_VA_LIMIT)) return 0;
    if (!interrupt_restart_admit(execution,c->sequence,frame->pc,frame->usp)) return 0;
    (void)interrupt_restart_suspend(execution);
    if (!interrupt_restart_return_valid(execution) || !restart_binding(space,frame,&b) ||
        memcmp(&b,&c->binding,sizeof(b)) || !m68k_irq_user_valid(&c->image,&b,USER_VA_LIMIT) ||
        !m68k_irq_return_controls_valid(&b) || !m68k_irq_fpu_idle(&b,&c->fpu) ||
        memcmp(frame,&c->image,sizeof(*frame))) panic("IRQ restart: m68k retained return binding changed");
#if CONF_WITH_IRQ_RESTART_TEST
    *(volatile ULONG *)&frame->d[1]=0;
#endif
    memcpy(frame,&c->image,sizeof(*frame));
    interrupt_restart_ack(execution);
    /* The outer assembly epilogue restores FPU after this final C return. */
    return fputype ? (ULONG)(uintptr_t)&c->fpu : 0;
}
#endif
#endif
