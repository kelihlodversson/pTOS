/* ARM IRQ image: shared C/assembly offsets, capture only, never a restore ABI. */
#ifndef ARM_IRQFRAME_H
#define ARM_IRQFRAME_H
#include "fpusave.h"
#define ARM_IRQ_BANKED FPU_ALL_BYTES
#define ARM_IRQ_R0 (ARM_IRQ_BANKED+24)
#define ARM_IRQ_SVC_LR (ARM_IRQ_R0+52)
#define ARM_IRQ_PC (ARM_IRQ_R0+56)
#define ARM_IRQ_CPSR (ARM_IRQ_R0+60)
#define ARM_IRQ_BYTES (ARM_IRQ_R0+64)
#ifndef ASM_SOURCE
#include "portab.h"
#include <stddef.h>
typedef struct {
#if FPU_ALL_BYTES
    UBYTE vfp[FPU_ALL_BYTES]; /* FPSCR/pad, D16-D31 if present, then D0-D15. */
#endif
    /* IRQ SP may be zero: SRS uses only the retained SVC stack. */
    ULONG user_sp, user_lr, irq_sp, irq_lr, svc_sp, svc_lr;
    ULONG r[13], saved_svc_lr, pc, cpsr;
} __attribute__((aligned(8))) ARM_USER_IRQ_FRAME;
typedef char arm_irq_banked[offsetof(ARM_USER_IRQ_FRAME,user_sp)==ARM_IRQ_BANKED ? 1:-1];
typedef char arm_irq_r0[offsetof(ARM_USER_IRQ_FRAME,r)==ARM_IRQ_R0 ? 1:-1];
typedef char arm_irq_svc_lr[offsetof(ARM_USER_IRQ_FRAME,saved_svc_lr)==ARM_IRQ_SVC_LR ? 1:-1];
typedef char arm_irq_pc[offsetof(ARM_USER_IRQ_FRAME,pc)==ARM_IRQ_PC ? 1:-1];
typedef char arm_irq_cpsr[offsetof(ARM_USER_IRQ_FRAME,cpsr)==ARM_IRQ_CPSR ? 1:-1];
typedef char arm_irq_size[sizeof(ARM_USER_IRQ_FRAME)==ARM_IRQ_BYTES ? 1:-1];
typedef struct {
    ULONG root, expected_root, sctlr, ttbcr, dacr, context_id, fpexc;
    uintptr_t stack_base, stack_top, frame_address;
} ARM_IRQ_BINDING;
static inline BOOL arm_irq_user_valid(const ARM_USER_IRQ_FRAME *f,
                                      const ARM_IRQ_BINDING *b, ULONG limit)
{
    return (f->cpsr&0x3f)==0x10 && !(f->cpsr&0x200) &&
        f->pc && f->pc<limit && !(f->pc&3) &&
        f->user_sp && f->user_sp<limit && !(f->user_sp&7) &&
        f->irq_lr==f->pc && f->svc_sp==b->stack_top &&
        f->svc_lr==f->saved_svc_lr && b->root && b->root==b->expected_root &&
        b->frame_address>=b->stack_base && b->frame_address<=~(uintptr_t)0-ARM_IRQ_BYTES &&
        !(b->frame_address&7) && b->frame_address+ARM_IRQ_BYTES==b->stack_top;
}
typedef struct {
    ARM_USER_IRQ_FRAME image;
    ARM_IRQ_BINDING binding;
    UQUAD execution, sequence;
    ULONG tid;
    UWORD cpu;
    BOOL valid;
} ARM_USER_IRQ_CAPTURE;
struct _pd;
void arm_capture_user_irq(const ARM_USER_IRQ_FRAME *frame);
BOOL arm_process_irq_stack(struct _pd *pd, uintptr_t *base, uintptr_t *top);
#if CONF_WITH_EXIT_HANDOFF_TEST
void arm_irq_capture_report(void);
#endif
#endif
#ifdef ASM_SOURCE
/* Registers and FPU are restored from this original image, never its copy.
 * IRQ remains masked. SRS wrote return PC/SPSR onto the SVC stack before this.
 * Shared registers are saved before scratch use or banked-mode inspection. */
.macro ARM_IRQ_PUSH_CAPTURE
    stmfd sp!, {r0-r12,lr}
    sub sp,sp,#24
    stmia sp,{r13-r14}^
    nop
    mov r0,sp
    cps #0x12
    str sp,[r0,#8]
    str lr,[r0,#12]
    cps #0x13
    add r1,sp,#88
    str r1,[sp,#16]
    ldr r1,[sp,#76]
    str r1,[sp,#20]
    FPU_PUSH_ALL r0
    mov r0,sp
    bl C_SYM(arm_capture_user_irq)
.endm
.macro ARM_IRQ_POP_CAPTURE
    FPU_POP_ALL r0
    add sp,sp,#24
    ldmfd sp!, {r0-r12,lr}
.endm
#endif
#endif
