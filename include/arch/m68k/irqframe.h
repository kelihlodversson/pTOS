/* Actual hardware user IRQ frame, not a synthetic timer callback frame. */
#ifndef M68K_IRQFRAME_H
#define M68K_IRQFRAME_H
#define M68K_IRQ_D0 4
#define M68K_IRQ_A0 36
#define M68K_IRQ_SR 64
#define M68K_IRQ_PC 66
#define M68K_IRQ_FORMAT 70
#define M68K_IRQ_BYTES 72
#ifndef ASM_SOURCE
#include "portab.h"
#include <stddef.h>
#include "fpuctx.h"
typedef struct {
    ULONG usp, d[8], a[7];
    UWORD sr;
    ULONG pc;
    UWORD format_vector;
} __attribute__((packed,aligned(2))) M68K_USER_IRQ_FRAME;
typedef char m68k_irq_d0[offsetof(M68K_USER_IRQ_FRAME,d)==M68K_IRQ_D0 ? 1:-1];
typedef char m68k_irq_a0[offsetof(M68K_USER_IRQ_FRAME,a)==M68K_IRQ_A0 ? 1:-1];
typedef char m68k_irq_sr[offsetof(M68K_USER_IRQ_FRAME,sr)==M68K_IRQ_SR ? 1:-1];
typedef char m68k_irq_pc[offsetof(M68K_USER_IRQ_FRAME,pc)==M68K_IRQ_PC ? 1:-1];
typedef char m68k_irq_format[offsetof(M68K_USER_IRQ_FRAME,format_vector)==M68K_IRQ_FORMAT ? 1:-1];
typedef char m68k_irq_size[sizeof(M68K_USER_IRQ_FRAME)==M68K_IRQ_BYTES ? 1:-1];
typedef struct {
    ULONG root, expected_root, model, fpu_type;
    ULONG tc, roots[4], tt[4]; /* model-tagged observation, not another MMU backend */
    uintptr_t stack_base, stack_top, frame_address;
} M68K_IRQ_BINDING;
/* Values are the hardware _FPU cookies returned by processor.S. A nonzero
 * value alone does not authorize FSAVE: SFP004 is memory-mapped, and a frame
 * from another CPU/FPU pair is not an interchangeable restore image. */
static inline BOOL m68k_irq_fpu_supported(ULONG model, ULONG fpu)
{
    if (model!=30 && model!=40 && model!=60) return FALSE;
    if (!fpu) return TRUE;
    if (model==30) return fpu==0x20000UL || fpu==0x40000UL || fpu==0x60000UL;
    return model==40 ? fpu==0x80000UL : fpu==0x100000UL;
}
/* Only hardware format-0 interrupt frames are admitted. Bus/fault, throwaway
 * and extended formats require separate exact model-specific decoders before
 * they can ever be captured. Never read their payload as this fixed image. */
static inline BOOL m68k_irq_user_valid(const M68K_USER_IRQ_FRAME *f,
                                      const M68K_IRQ_BINDING *b, ULONG limit)
{
    return m68k_irq_fpu_supported(b->model,b->fpu_type) &&
        !(f->sr&0x3000) && !(f->format_vector&0xf000) &&
        !(f->format_vector&3) && (f->format_vector&0xfff)>=24*4 &&
        (f->format_vector&0xfff)<=255*4 && f->pc && f->pc<limit && !(f->pc&1) &&
        f->usp && f->usp<limit && !(f->usp&1) &&
        b->root && b->root==b->expected_root &&
        b->frame_address>=b->stack_base && !(b->frame_address&1) &&
        b->frame_address<=~(uintptr_t)0-M68K_IRQ_BYTES &&
        b->frame_address+M68K_IRQ_BYTES==b->stack_top;
}
typedef struct {
    M68K_USER_IRQ_FRAME image;
    M68K_IRQ_BINDING binding;
    struct fpuctx fpu;
    UQUAD execution, sequence;
    ULONG tid;
    UWORD cpu;
    BOOL valid, complete_fpu;
} M68K_USER_IRQ_CAPTURE;
/* Only proven null/idle headers qualify for the restricted copy-back proof.
 * FSAVE remains opaque; no guessed exception or busy-frame length is copied. */
static inline BOOL m68k_irq_fpu_idle(const M68K_IRQ_BINDING *b, const struct fpuctx *f)
{
    if (!m68k_irq_fpu_supported(b->model,b->fpu_type)) return FALSE;
    if (!b->fpu_type) return TRUE;
    if (b->model==40)
        return (!f->frame[0] || f->frame[0]==0x40 || f->frame[0]==0x41) &&
            !f->frame[1] && !f->frame[2] && !f->frame[3];
    /* 030/6888x pending BIU state and 060 exception-state restoration require
     * separate native proof. Their diagnostic captures remain supported. */
    return FALSE;
}
static inline BOOL m68k_irq_return_controls_valid(const M68K_IRQ_BINDING *b)
{
#if CONF_WITH_MFASTCALL
    (void)b;
    return FALSE;
#else
    if (b->model==30)
        return b->tc==0x80c07760UL && b->roots[0]==0x7fff0003UL && b->roots[1]==b->root &&
            !(b->tt[0]&0x8000UL) && !(b->tt[1]&0x8000UL);
    return (b->model==40 || b->model==60) && b->tc==0x8000UL &&
        b->roots[0]==b->root && b->roots[1]==b->root;
#endif
}
static inline BOOL m68k_irq_same_entry_valid(const M68K_USER_IRQ_CAPTURE *c,
    const M68K_USER_IRQ_FRAME *original, const M68K_IRQ_BINDING *b,
    UQUAD execution, UQUAD sequence, ULONG tid, UWORD cpu, ULONG limit)
{
    const M68K_IRQ_BINDING *s=&c->binding;
    const UBYTE *a=(const UBYTE *)&c->image,*o=(const UBYTE *)original;
    ULONG i;
    /* The copied-return proof is qualified only on 040, including no-FPU.
     * 030/060 control observations are preparation, not native copy-back proof. */
    if (b->model!=40 || !c->valid || !execution || c->execution!=execution || !sequence || c->sequence!=sequence ||
        c->tid!=tid || c->cpu!=cpu || b->frame_address!=(uintptr_t)original ||
        s->root!=b->root || s->expected_root!=b->expected_root || s->model!=b->model ||
        s->fpu_type!=b->fpu_type || s->tc!=b->tc || s->stack_base!=b->stack_base ||
        s->stack_top!=b->stack_top || s->frame_address!=b->frame_address ||
        !m68k_irq_user_valid(original,b,limit) || !m68k_irq_user_valid(&c->image,b,limit) ||
        !m68k_irq_return_controls_valid(b) || !m68k_irq_fpu_idle(b,&c->fpu) ||
        (b->fpu_type && !c->complete_fpu)) return FALSE;
    for(i=0;i<4;i++) if(s->roots[i]!=b->roots[i] || s->tt[i]!=b->tt[i]) return FALSE;
    for(i=0;i<sizeof(*original);i++) if(a[i]!=o[i]) return FALSE;
    return TRUE;
}
struct _pd;
void m68k_capture_user_irq(const M68K_USER_IRQ_FRAME *frame);
BOOL m68k_process_irq_stack(struct _pd *pd, uintptr_t *base, uintptr_t *top);
#if CONF_WITH_EXIT_HANDOFF_TEST
void m68k_irq_capture_report(void);
extern ULONG m68k_irq_test_state[3];
#endif
#else
/* Brief local IRQ exclusion protects the single-CPU captured image, not SMP.
 * The original hardware exception and saved GPR image alone drive RTE. */
.macro M68K_IRQ_CAPTURE
    movem.l d0-d7/a0-a6,-(sp)
    move.l usp,a0
    move.l a0,-(sp)
    subq.l #4,sp
    move.w sr,(sp)
    ori.w #0x0700,sr
    lea 4(sp),a0
#if CONF_WITH_MFASTCALL
    jsr C_SYM(m68k_capture_user_irq)
#else
    move.l a0,-(sp)
    jsr C_SYM(m68k_capture_user_irq)
    addq.l #4,sp
#endif
    move.w (sp),sr
    addq.l #4,sp
    move.l (sp)+,a0
    move.l a0,usp /* Consume the user-bank prefix before restoring A0. */
    movem.l (sp)+,d0-d7/a0-a6
.endm
#endif
#endif
