/* Native 32-bit initial entry contract. CPU entry remains architecture-owned. */
#ifndef THREAD_STARTUP32_H
#define THREAD_STARTUP32_H
#include "emutos.h"
#include "thread_startup.h"
#include "kthread_context.h"
typedef struct {
    VADDR entry, sp, argument, return_pc;
    volatile UWORD *frame_aliases[4];
    KTHREAD_STACK scheduler_stack;
    THREAD_STARTUP_STACK syscall;
} THREAD_STARTUP32;
#endif
