/* Loaded callback/watchdog execution, never a shared callback registry.
 * No callback may suspend or migrate while this chain owns continuations. */
#ifndef CALLBACK_CPU_H
#define CALLBACK_CPU_H
#include "portab.h"
struct callback_frame;
typedef struct {
    struct callback_frame *frame;
    ULONG token;
    UWORD depth;
} USER_CALLBACK_CPU;
#if CONF_WITH_NATIVE_SCHED
USER_CALLBACK_CPU *ksched_callback_cpu(void);
#endif
#endif
