/*
 * process.h - x86-64 kernel process context and typed user entry
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef X86_64_PROCESS_H
#define X86_64_PROCESS_H

#include "portab.h"
#include "bdosdefs.h"
#include "setjmp.h"

#define ENTRY_PROGRAM   0
#define ENTRY_ACCESSORY 1

/* Kernel-owned state accompanying a suspended kernel coroutine stack.
 * Saving normalises GS for the scheduler; restoring reinstates the task's
 * syscall state. The saved continuation stays on that task's own stack. */
typedef struct x86_64_process_launch X86_64_PROCESS_LAUNCH;
typedef struct {
    PD *pd;
    UQUAD cr3, kernel_rsp, user_rsp, irq_rsp, flags;
    X86_64_PROCESS_LAUNCH *launch;
    jmp_buf disk_error;
    int in_syscall;
    UBYTE fpu[512] __attribute__((aligned(16)));
} X86_64_PROCESS_CONTEXT;

void x86_64_process_save(X86_64_PROCESS_CONTEXT *context);
void x86_64_process_restore(const X86_64_PROCESS_CONTEXT *context);
void gouser_entry(WORD entry_type);
BOOL x86_64_process_is_accessory(void);
void x86_64_process_cancel(X86_64_PROCESS_CONTEXT *context, PD *root);
void proc_dispose_detached(PD *pd);

/* Kernel-only accessory launch: preparation can fail before the AES task
 * is published. start consumes a successfully launched basepage, including
 * on a user fault; discard consumes an unlaunched basepage. */
LONG proc_prepare_accessory(PD *pd);
LONG proc_start_accessory(PD *pd);
void proc_discard_accessory(PD *pd);

#endif
