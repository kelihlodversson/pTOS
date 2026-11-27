#ifndef FPOLL_KERNEL_H
#define FPOLL_KERNEL_H

#if CONF_WITH_FPOLL_TIME64
#include "fpoll_abi.h"

LONG xfpoll_time64(UQUAD address, UQUAD nfds, QUAD timeout);
#if ARCH_M68K
LONG xfselect(UWORD timeout, ULONG read_mask, ULONG write_mask, ULONG except_mask);
#endif

/* Single boot CPU. Timer C publishes the monotonic tick in IRQ context.
 * A provider may notify after publishing a change to its readiness level.
 * Notification retains no driver pointer and never dispatches a waiter. */
void fpoll_tick(void);
void fpoll_notify(void);
#endif
#endif
