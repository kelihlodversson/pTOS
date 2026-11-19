/* Authentic user IRQ capture and the shared B2 retained-return backend.
 * Timer policy and test-only preparation are separate admission gates. */
#include "emutos.h"

#if CONF_WITH_NATIVE_SCHED && CONF_WITH_USER_ASPACE
#include "kthread_context.h"
#include "ksched.h"
#include "kaspace.h"
#include "../../../bios/arch/x86_64/trap.h"
#include "../../../bios/arch/x86_64/gdt.h"
#include "../../../bios/arch/x86_64/pgtable.h"
#include "string.h"
#include "biosext.h"
extern PD *run;
#if CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_X86_64_MEMTEST
static ULONG irq_pattern_seen, irq_roundtrip_seen;
static BOOL irq_pattern_bad, irq_pattern_reported;
static void check_irq_pattern(const X86_USER_IRQ_FRAME *f)
{
    UWORD i;
    if (f->r15!=0x7130000000713015ULL) return; /* set last, cleared first by user probe */
    irq_pattern_seen++;
    if (!(f->rflags&0x400) || f->rax!=0x7130000000713001ULL || f->rbx!=0x7130000000713002ULL || f->rcx>2000000 ||
        f->rdx!=0x7130000000713004ULL || f->rsi!=0x7130000000713005ULL || f->rdi!=0x7130000000713006ULL ||
        f->rbp!=0x7130000000713007ULL || f->r8!=0x7130000000713008ULL || f->r9!=0x7130000000713009ULL ||
        f->r10!=0x7130000000713010ULL || f->r11!=0x7130000000713011ULL || f->r12!=0x7130000000713012ULL ||
        f->r13!=0x7130000000713013ULL || f->r14!=0x7130000000713014ULL ||
        f->fx[0]!=0x7f || f->fx[1]!=3 || f->fx[4]!=0x80 ||
        f->fx[39]!=0x80 || f->fx[40]!=0xff || f->fx[41]!=0x3f)
        irq_pattern_bad=TRUE;
    for (i=0;i<8;i++) if (f->fx[32+i]!=(i==7 ? 0x80 : 0)) irq_pattern_bad=TRUE;
    for (i=0;i<256;i++) if (f->fx[160+i]!=(i&1 ? 0x71 : 0x30)) irq_pattern_bad=TRUE;
    if (f->fx[24]!=0x80 || f->fx[25]!=0x1f) irq_pattern_bad=TRUE;
}
void x86_64_irq_capture_report(void)
{
    if (irq_pattern_bad) panic("x32 interrupted-user register capture mismatch");
    if (!irq_pattern_seen || irq_pattern_reported) return;
    irq_pattern_reported=TRUE;
    kprintf("native x32 interrupted-user GPR/FXSAVE/iret capture: PASS\r\n");
    if (!irq_roundtrip_seen) panic("x32 copied IRQ round trip not exercised");
    kprintf("native x32 fresh owned IRQ image round trip: PASS\r\n");
}
#endif
void x86_64_capture_user_irq(const X86_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    X86_USER_IRQ_CAPTURE *capture;
    X86_IRQ_BINDING binding;
    ULONG tid;
    /* Wake metadata only; no kernel-frame capture or scheduler transfer. IRQ
     * readiness is durable before this handler returns on the original frame. */
    ksched_irq_notify();
    if (frame->cs!=X86_64_USER_CODE_SEL) return; /* no kernel-frame interpretation */
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution) return;
    binding.cr3=x86_64_read_cr3();
    binding.expected_cr3=aspace_root(kaspace_backend(space));
    binding.syscall_top=x86_64_get_kernel_stack();
    binding.irq_top=x86_64_get_irq_stack();
    binding.frame_address=(UQUAD)(uintptr_t)frame;
    x86_64_irq_read_controls(&binding.controls);
    capture=&execution->interrupted_user;
    capture->valid=FALSE;
    if (!x86_irq_user_valid(frame,&binding,USER_VA_LIMIT) || capture->sequence==~(UQUAD)0) return;
    memcpy(&capture->image,frame,sizeof(*frame));
    capture->binding=binding;
    capture->execution=execution->identity;
    capture->tid=tid; capture->cpu=execution->owner_cpu;
    capture->sequence++;
    capture->valid=TRUE;
#if CONF_WITH_IRQ_RESTART
    interrupt_restart_user_capture(execution);
#endif
#if CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_X86_64_MEMTEST
    /* Keep the diagnostic image, but an unsupported proof must neither alter
     * IRQ return nor leave a pattern count that makes root demand copy-back. */
    if (!x86_irq_return_controls_valid(&binding.controls)) return;
    check_irq_pattern(&capture->image);
    /* Restricted proof BEFORE dispatch/callbacks: no yield, reference release,
     * mapping change or GS/FS/TSS change can intervene. The original IRQ stack
     * remains owned and live. Never use a prior capture after a scheduling or
     * callback boundary. Unsupported/delayed restore remains unavailable. */
    if (capture->image.r15==0x7130000000713015ULL) {
        X86_USER_IRQ_FRAME *destination=(X86_USER_IRQ_FRAME *)(uintptr_t)frame;
        UWORD byte;
        if (!x86_irq_same_entry_valid(capture,frame,&binding,execution->identity,
                                     capture->sequence,tid,execution->owner_cpu,USER_VA_LIMIT))
            panic("x32 copied IRQ image ownership mismatch");
        /* Deliberately clobber live saved fields so native user register checks
         * fail if the owned image is not actually copied back. Keep control
         * state untouched until the complete validated image is restored. */
        *(volatile UQUAD *)&destination->rax=0;
        for(byte=160;byte<416;byte++) ((volatile UBYTE *)destination->fx)[byte]=0;
        __asm__ volatile ("" ::: "memory");
        memcpy(destination,&capture->image,sizeof(*destination));
        irq_roundtrip_seen++;
    }
#endif
}
#if CONF_WITH_IRQ_RESTART
void x86_64_restart_user_irq(X86_USER_IRQ_FRAME *frame)
{
    KTHREAD_CONTEXT *execution;
    KASPACE *space;
    ULONG tid;
    X86_USER_IRQ_CAPTURE *c;
    X86_IRQ_BINDING b;
    if (frame->cs!=X86_64_USER_CODE_SEL) return;
    execution=ksched_interrupt_target(run,&space,&tid);
    if (!execution || execution->irq_restart.state!=IRQ_RESTART_ARMED) return;
    c=&execution->interrupted_user;
    b.cr3=x86_64_read_cr3(); b.expected_cr3=aspace_root(kaspace_backend(space));
    b.syscall_top=x86_64_get_kernel_stack(); b.irq_top=x86_64_get_irq_stack();
    b.frame_address=(UQUAD)(uintptr_t)frame;
    x86_64_irq_read_controls(&b.controls);
    if (!x86_irq_same_entry_valid(c,frame,&b,execution->identity,c->sequence,tid,
                                 execution->owner_cpu,USER_VA_LIMIT) ||
        !interrupt_restart_admit(execution,c->sequence,(VADDR)frame->rip,(VADDR)frame->rsp)) return;
    (void)interrupt_restart_suspend(execution);
    /* Native context restoration selected A's CR3/TSS/GS phase. Restore its
     * supported segment image before validating the exact final binding. */
    x86_64_irq_restore_user_segments(&c->binding.controls);
    b.cr3=x86_64_read_cr3(); b.expected_cr3=aspace_root(kaspace_backend(space));
    b.syscall_top=x86_64_get_kernel_stack(); b.irq_top=x86_64_get_irq_stack();
    x86_64_irq_read_controls(&b.controls);
    if (!interrupt_restart_return_valid(execution) ||
        b.cr3!=c->binding.cr3 || b.expected_cr3!=c->binding.expected_cr3 ||
        b.syscall_top!=c->binding.syscall_top || b.irq_top!=c->binding.irq_top ||
        !x86_irq_user_valid(&c->image,&b,USER_VA_LIMIT) ||
        memcmp(frame,&c->image,sizeof(*frame))) panic("IRQ restart: x32 retained return binding changed");
    /* This corruption occurs only AFTER real B user execution was observed.
     * A's native GPR/FXSAVE probe fails unless the frozen owned image wins. */
#if CONF_WITH_IRQ_RESTART_TEST
    *(volatile UQUAD *)&frame->rax=0;
    memset(frame->fx+160,0,256);
#endif
    memcpy(frame,&c->image,sizeof(*frame));
    interrupt_restart_ack(execution);
}
#endif
#endif
