/* Kernel-private CUIS entry points. */
#ifndef CUIS_KERNEL_H
#define CUIS_KERNEL_H
#include "bdosdefs.h"
#include "../include/cuis.h"
LONG cuis_service(ULONG function, UQUAD handle);
LONG cuis_window_service(ULONG function, UQUAD a, UQUAD b, UQUAD c,
                         UQUAD d, UQUAD e);
LONG cuis_clip_service(UQUAD handle, UQUAD index, UQUAD x, UQUAD y,
                       UQUAD width, UQUAD height);
void cuis_process_exit(PD *owner);
#if CONF_WITH_X86_64_MEMTEST
ULONG cuis_session_count(void);
BOOL cuis_update_active(void);
ULONG cuis_window_count(void);
ULONG cuis_visible_window_count(void);
#endif
#endif
