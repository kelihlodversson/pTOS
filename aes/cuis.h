/* Kernel-private CUIS entry points. */
#ifndef CUIS_KERNEL_H
#define CUIS_KERNEL_H
#include "bdosdefs.h"
#include "../include/cuis.h"
LONG cuis_service(ULONG function, UQUAD handle);
void cuis_process_exit(PD *owner);
#if CONF_WITH_X86_64_MEMTEST
ULONG cuis_session_count(void);
#endif
#endif
