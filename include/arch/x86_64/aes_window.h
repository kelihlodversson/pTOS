/* Kernel-owned native windows for associated x32 AES clients. */
#ifndef X86_64_AES_WINDOW_H
#define X86_64_AES_WINDOW_H
#include "aes_service.h"
LONG x86_64_aes_window(PD *pd, const AES_WINDOW_REQUEST *request,
                       const char *text, AES_WINDOW_INFO *info, GRECT *rectangle);
void x86_64_aes_window_exit(PD *pd);
void x86_64_aes_window_finished(PD *pd);
void x86_64_aes_window_unregister(PD *pd);
void x86_64_aes_window_drain(void);
BOOL x86_64_aes_window_redraw(PD *pd, const WORD *message);
BOOL x86_64_aes_window_pending(PD *pd, AES_MESSAGE *message);
#if CONF_WITH_X32_ACCESSORY_TEST
void x86_64_aes_window_test(void);
BOOL x86_64_aes_window_test_ready(void);
BOOL x86_64_aes_window_test_revoke(PD *pd);
BOOL x86_64_aes_window_test_reuse(PD *pd);
#endif
#endif
