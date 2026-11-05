/* Initial user CPU image, not the suspended kernel-coroutine context. */
#ifndef THREAD_STARTUP_X32_H
#define THREAD_STARTUP_X32_H
#include "emutos.h"
#include <aspace_types.h>
#include "kthread_context.h"
typedef struct {
    UQUAD rax, rbx, rcx, rdx, rsi, rdi, rbp;
    UQUAD r8, r9, r10, r11, r12, r13, r14, r15;
    UQUAD rip, rsp, rflags, cr3;
    UQUAD cs, ss, ds, es, fs, gs, fs_base, gs_base;
    UBYTE fpu[512] __attribute__((aligned(16)));
} X32_THREAD_ENTRY_IMAGE;
typedef struct {
    X32_THREAD_ENTRY_IMAGE user;
    /* Planned 8-byte return slot at user.rsp. It is NOT written by prepare. */
    UQUAD return_pc;
    KTHREAD_STACK scheduler_stack;
    PADDR syscall_physical;
    uintptr_t syscall_top;
} X32_THREAD_STARTUP_DRAFT;
/* Separate release owners: launcher after syscall abandonment, scheduler root
 * after task continuation is irrevocably stopped. Moving these never starts a
 * context. Cancellation is still on a verified different stack. */
typedef struct {
    KTHREAD_STACK syscall, scheduler;
} X32_THREAD_STACK_OWNERS;
BOOL thread_startup_x32_take_stacks(X32_THREAD_STARTUP_DRAFT *draft, X32_THREAD_STACK_OWNERS *owners);
void thread_startup_x32_release_syscall(X32_THREAD_STACK_OWNERS *owners);
void thread_startup_x32_release_scheduler(X32_THREAD_STACK_OWNERS *owners);
#if CONF_WITH_X86_64_MEMTEST
/* Fail after metadata, scheduler stack, or syscall stack acquisition. */
void thread_startup_x32_test_fail_after(UWORD stage);
#endif
#endif
