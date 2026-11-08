/* CPU mechanics of the shared cooperative dispatcher. No scheduling policy. */
#ifndef NATIVE_SCHED_BACKEND_H
#define NATIVE_SCHED_BACKEND_H
#include "emutos.h"
#include "bdosdefs.h"
#include "setjmp.h"
#if ARCH_ARM
#include "asm.h"
#endif
#if ARCH_X86_64
#include "process.h"
typedef X86_64_PROCESS_CONTEXT NATIVE_SCHED_CONTEXT;
#define native_sched_save x86_64_process_save
#define native_sched_restore x86_64_process_restore
/* native_sched_enter jumps to a SysV C entry: RSP must be 8 mod 16. */
static inline uintptr_t native_sched_root_stack_top(uintptr_t base, ULONG bytes)
{ return base + bytes - 8; }
static inline uintptr_t native_sched_irq_save(void)
{
    uintptr_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void native_sched_irq_disable(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void native_sched_irq_restore(uintptr_t flags)
{
    if (flags & 0x200) __asm__ volatile ("sti" ::: "memory");
}
static inline uintptr_t native_sched_masked(uintptr_t flags) { return flags & ~0x200UL; }
static inline void native_sched_idle(void) { __asm__ volatile ("sti; hlt; cli" ::: "memory"); }
#else
/* A continuation lives on its own supervisor stack. setjmp retains C's
 * callee-saved registers; trap frames on that stack retain user registers. */
#if ARCH_M68K
struct fpuctx {
    UBYTE frame[216];
    ULONG ctl[3];
    UBYTE regs[96];
};
#endif
typedef struct {
    PD *pd;
    uintptr_t flags;
    ULONG root, user_sp;
#if ARCH_ARM
    ULONG user_lr;
#ifdef __ARM_PCS_VFP
    ULONG fpscr;
#endif
#endif
    void *launch;
    UQUAD native_task;
    jmp_buf disk_error;
#if ARCH_M68K
    struct fpuctx fpu;
#endif
} NATIVE_SCHED_CONTEXT;
void native_sched_save(NATIVE_SCHED_CONTEXT *context);
void native_sched_restore(const NATIVE_SCHED_CONTEXT *context);
/* ARM/m68k reuse the supervisor-stack C-entry ABI of native task entry. */
static inline uintptr_t native_sched_root_stack_top(uintptr_t base, ULONG bytes)
{ return base + bytes; }
#if ARCH_ARM
static inline uintptr_t native_sched_irq_save(void)
{
    uintptr_t flags;
    __asm__ volatile ("mrs %0,cpsr; cpsid i" : "=r"(flags) :: "memory", "cc");
    return flags;
}
static inline void native_sched_irq_disable(void) { __asm__ volatile ("cpsid i" ::: "memory"); }
static inline void native_sched_irq_restore(uintptr_t flags)
{ __asm__ volatile ("msr cpsr_c,%0" :: "r"(flags) : "memory", "cc"); }
static inline uintptr_t native_sched_masked(uintptr_t flags) { return flags | 0x80; }
/* IRQs service the clock/devices; the dispatcher remains cooperative. */
/* WFI observes a pending IRQ even while CPSR.I is set. Do not enable IRQs
 * before WFI: an IRQ could run/clear its source in that gap and leave a ready
 * queue asleep. Service the pending IRQ after waking, then return masked. */
static inline void native_sched_idle(void)
{
    data_sync_barrier();
    __asm__ volatile ("wfi; cpsie i" ::: "memory", "cc");
    instruction_sync_barrier();
    __asm__ volatile ("cpsid i" ::: "memory", "cc");
}
#else
static inline uintptr_t native_sched_irq_save(void)
{
    UWORD flags;
    __asm__ volatile ("move.w %%sr,%0; ori.w #0x0700,%%sr" : "=d"(flags) :: "memory", "cc");
    return flags;
}
static inline void native_sched_irq_disable(void) { __asm__ volatile ("ori.w #0x0700,%%sr" ::: "memory", "cc"); }
static inline void native_sched_irq_restore(uintptr_t flags)
{ __asm__ volatile ("move.w %0,%%sr" :: "d"((UWORD)flags) : "memory", "cc"); }
static inline uintptr_t native_sched_masked(uintptr_t flags) { return flags | 0x0700; }
static inline void native_sched_idle(void) { __asm__ volatile ("stop #0x2000; ori.w #0x0700,%%sr" ::: "memory", "cc"); }
#endif
#endif
/* Remove every borrowed caller execution identity from root CPU storage.
 * Root never enters user mode; these supervisor stacks are kernel-owned. */
static inline void native_sched_root_context(NATIVE_SCHED_CONTEXT *c,PD *pd,uintptr_t top)
{
    UWORD i;
    c->pd=pd; c->launch=NULL;
#if CONF_WITH_NATIVE_SCHED
    c->native_task=0;
#endif
    c->flags=native_sched_masked(c->flags);
    for (i=0;i<sizeof(c->disk_error);i++) ((UBYTE *)c->disk_error)[i]=0;
#if ARCH_X86_64
    c->kernel_rsp=top; c->irq_rsp=top+8; c->user_rsp=0; c->in_syscall=0;
#else
    (void)top;
    c->user_sp=0;
#if ARCH_ARM
    c->user_lr=0;
#endif
#endif
}
void native_sched_enter(uintptr_t top, void (*entry)(void)) NORETURN;
#endif
