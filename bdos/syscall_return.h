/* Kernel-owned user state, never a kernel continuation or user ABI record. */
#ifndef SYSCALL_RETURN_H
#define SYSCALL_RETURN_H
#include "emutos.h"
#include "thread_startup.h"
#include <stddef.h>
struct kthread;
struct kaspace;
struct proc_clone_transaction;
#if CONF_WITH_FORK_RETURN_TEST
#if ARCH_X86_64
#include "arch/x86_64/thread_startup_x32.h"
typedef X32_THREAD_ENTRY_IMAGE SYSCALL_USER_IMAGE;
#elif ARCH_ARM
#include "arch/arm/fpusave.h"
typedef struct {
#if FPU_ALL_BYTES
    ULONG fpu[FPU_ALL_BYTES / 4];
#endif
    ULONG usp, ulr, r[13], pad, pc, status;
} SYSCALL_USER_IMAGE;
#else
typedef struct {
    ULONG usp, d[8], a[7];
    UWORD sr;
    ULONG pc;
    UWORD format;
} PACKED SYSCALL_M68K_FRAME;
typedef struct {
    SYSCALL_M68K_FRAME frame;
    UBYTE fpu[324] __attribute__((aligned(4)));
} SYSCALL_USER_IMAGE;
typedef char syscall_m68k_layout_check[(sizeof(SYSCALL_M68K_FRAME) == 72 &&
    offsetof(SYSCALL_M68K_FRAME, sr) == 64 && offsetof(SYSCALL_M68K_FRAME, pc) == 66 &&
    offsetof(SYSCALL_M68K_FRAME, format) == 70 && offsetof(SYSCALL_USER_IMAGE, fpu) == 72) ? 1 : -1];
#endif
typedef struct syscall_return {
    struct kthread *owner; /* borrowed only during this authenticated invocation */
    struct kaspace *space;
    THREAD_STARTUP_ARGS mappings;
    SYSCALL_USER_IMAGE user;
} SYSCALL_RETURN;
/* Reserved Pclone(NULL, size) test transport; no production ABI assignment. */
#define FORK_RETURN_PROBE 0x895f0000UL
LONG proc_clone_return_probe(SYSCALL_RETURN *, UWORD fail_stage);
LONG proc_clone_prepare_saved_return(struct proc_clone_transaction *, const SYSCALL_RETURN *);
LONG thread_startup_prepare_saved_return(struct kaspace *, const SYSCALL_RETURN *, THREAD_STARTUP_DRAFT **);
LONG thread_startup_arch_saved_return(void *image, const SYSCALL_RETURN *);
#endif
#endif
