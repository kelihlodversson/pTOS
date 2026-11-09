/* IRQ entry image. No delayed restore/dispatch capability. */
#ifndef X86_64_IRQFRAME_H
#define X86_64_IRQFRAME_H
#define X86_IRQ_FX_BYTES 512
#define X86_IRQ_R15 512
#define X86_IRQ_R14 520
#define X86_IRQ_R13 528
#define X86_IRQ_R12 536
#define X86_IRQ_R11 544
#define X86_IRQ_R10 552
#define X86_IRQ_R9 560
#define X86_IRQ_R8 568
#define X86_IRQ_RBP 576
#define X86_IRQ_RDI 584
#define X86_IRQ_RSI 592
#define X86_IRQ_RDX 600
#define X86_IRQ_RCX 608
#define X86_IRQ_RBX 616
#define X86_IRQ_RAX 624
#define X86_IRQ_VECTOR 632
#define X86_IRQ_PADDING 640
#define X86_IRQ_RIP 648
#define X86_IRQ_CS 656
#define X86_IRQ_RFLAGS 664
#define X86_IRQ_RSP 672
#define X86_IRQ_SS 680
#define X86_IRQ_BYTES 688
#ifndef __ASSEMBLER__
#include "portab.h"
#include <stddef.h>
#include "../../../bios/arch/x86_64/gdt.h"
typedef struct {
    UBYTE fx[512];
    UQUAD r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax, vector, padding, rip, cs, rflags, rsp, ss;
} __attribute__((aligned(16))) X86_USER_IRQ_FRAME;
typedef char irq_offset_r15[offsetof(X86_USER_IRQ_FRAME,r15)==X86_IRQ_R15 ? 1 : -1];
typedef char irq_offset_r14[offsetof(X86_USER_IRQ_FRAME,r14)==X86_IRQ_R14 ? 1 : -1];
typedef char irq_offset_r13[offsetof(X86_USER_IRQ_FRAME,r13)==X86_IRQ_R13 ? 1 : -1];
typedef char irq_offset_r12[offsetof(X86_USER_IRQ_FRAME,r12)==X86_IRQ_R12 ? 1 : -1];
typedef char irq_offset_r11[offsetof(X86_USER_IRQ_FRAME,r11)==X86_IRQ_R11 ? 1 : -1];
typedef char irq_offset_r10[offsetof(X86_USER_IRQ_FRAME,r10)==X86_IRQ_R10 ? 1 : -1];
typedef char irq_offset_r9[offsetof(X86_USER_IRQ_FRAME,r9)==X86_IRQ_R9 ? 1 : -1];
typedef char irq_offset_r8[offsetof(X86_USER_IRQ_FRAME,r8)==X86_IRQ_R8 ? 1 : -1];
typedef char irq_offset_rbp[offsetof(X86_USER_IRQ_FRAME,rbp)==X86_IRQ_RBP ? 1 : -1];
typedef char irq_offset_rdi[offsetof(X86_USER_IRQ_FRAME,rdi)==X86_IRQ_RDI ? 1 : -1];
typedef char irq_offset_rsi[offsetof(X86_USER_IRQ_FRAME,rsi)==X86_IRQ_RSI ? 1 : -1];
typedef char irq_offset_rdx[offsetof(X86_USER_IRQ_FRAME,rdx)==X86_IRQ_RDX ? 1 : -1];
typedef char irq_offset_rcx[offsetof(X86_USER_IRQ_FRAME,rcx)==X86_IRQ_RCX ? 1 : -1];
typedef char irq_offset_rbx[offsetof(X86_USER_IRQ_FRAME,rbx)==X86_IRQ_RBX ? 1 : -1];
typedef char irq_offset_rax[offsetof(X86_USER_IRQ_FRAME,rax)==X86_IRQ_RAX ? 1 : -1];
typedef char irq_offset_vector[offsetof(X86_USER_IRQ_FRAME,vector)==X86_IRQ_VECTOR ? 1 : -1];
typedef char irq_offset_padding[offsetof(X86_USER_IRQ_FRAME,padding)==X86_IRQ_PADDING ? 1 : -1];
typedef char irq_offset_rip[offsetof(X86_USER_IRQ_FRAME,rip)==X86_IRQ_RIP ? 1 : -1];
typedef char irq_offset_cs[offsetof(X86_USER_IRQ_FRAME,cs)==X86_IRQ_CS ? 1 : -1];
typedef char irq_offset_rflags[offsetof(X86_USER_IRQ_FRAME,rflags)==X86_IRQ_RFLAGS ? 1 : -1];
typedef char irq_offset_rsp[offsetof(X86_USER_IRQ_FRAME,rsp)==X86_IRQ_RSP ? 1 : -1];
typedef char irq_offset_ss[offsetof(X86_USER_IRQ_FRAME,ss)==X86_IRQ_SS ? 1 : -1];
typedef char irq_frame_size[sizeof(X86_USER_IRQ_FRAME)==X86_IRQ_BYTES ? 1 : -1];
typedef struct {
    UQUAD cr3, expected_cr3, syscall_top, irq_top, frame_address;
} X86_IRQ_BINDING;
/* Trusted kernel image only: this never touches user memory or walks mappings.
 * FXSAVE covers the enabled x87/MMX/SSE state; OSXSAVE/AVX is not enabled here. */
static inline BOOL x86_irq_user_valid(const X86_USER_IRQ_FRAME *f,
                                     const X86_IRQ_BINDING *b, UQUAD user_limit)
{
    return f->cs==X86_64_USER_CODE_SEL && f->ss==X86_64_USER_DATA_SEL &&
        f->rip && f->rip<user_limit && f->rsp && f->rsp<user_limit &&
        (f->rflags & 2) && !(f->rflags & ((3ULL<<12)|(1ULL<<14)|(1ULL<<17))) &&
        (f->vector==32 || f->vector==33 || f->vector==36 || f->vector==44) && !f->padding &&
        b->cr3 && b->cr3==b->expected_cr3 && b->syscall_top &&
        b->syscall_top+8==b->irq_top && !(b->frame_address&15) &&
        b->frame_address<=~(UQUAD)0-X86_IRQ_BYTES &&
        b->frame_address+X86_IRQ_BYTES==b->irq_top;
}
typedef struct {
    X86_USER_IRQ_FRAME image;
    X86_IRQ_BINDING binding;
    UQUAD execution, sequence;
    ULONG tid;
    UWORD cpu;
    BOOL valid;
} X86_USER_IRQ_CAPTURE;
/* Test proof only: this eligibility never authorizes a delayed continuation.
 * Caller must be in the SAME IRQ entry, before dispatch/callback/yield. The
 * untouched original is the freshness witness, not only a cached stack VA. */
static inline BOOL x86_irq_same_entry_valid(const X86_USER_IRQ_CAPTURE *capture,
    const X86_USER_IRQ_FRAME *original, const X86_IRQ_BINDING *current,
    UQUAD execution, UQUAD sequence, ULONG tid, UWORD cpu, UQUAD limit)
{
    const X86_IRQ_BINDING *saved=&capture->binding;
    const UBYTE *a=(const UBYTE *)&capture->image,*b=(const UBYTE *)original;
    ULONG i;
    if (!capture->valid || !sequence || capture->sequence!=sequence ||
        !execution || capture->execution!=execution || capture->tid!=tid || capture->cpu!=cpu ||
        current->frame_address!=(UQUAD)(uintptr_t)original ||
        saved->frame_address!=current->frame_address || saved->cr3!=current->cr3 ||
        saved->expected_cr3!=current->expected_cr3 || saved->syscall_top!=current->syscall_top ||
        saved->irq_top!=current->irq_top || !x86_irq_user_valid(original,current,limit) ||
        !x86_irq_user_valid(&capture->image,current,limit)) return FALSE;
    for(i=0;i<sizeof(*original);i++) if(a[i]!=b[i]) return FALSE;
    return TRUE;
}
void x86_64_capture_user_irq(const X86_USER_IRQ_FRAME *frame);
#if CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_X86_64_MEMTEST
void x86_64_irq_capture_report(void); /* root only, never from IRQ */
#endif
#endif
#endif
