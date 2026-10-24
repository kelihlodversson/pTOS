/* Kernel-only AES coroutine and accessory ownership.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#ifndef X86_64_AES_ACCESSORY_H
#define X86_64_AES_ACCESSORY_H
#include "portab.h"
#include "bdosdefs.h"
#include "aes_client.h"

struct aespd;
#define X32_AES_DEAD 0x8000
void x86_64_aes_setup(struct aespd *pd, PD *accessory);
void x86_64_aes_save(void);
void x86_64_aes_restore(void);
void x86_64_aes_enter(void) NORETURN;
void x86_64_aes_launch_begin(PD *parent);
void x86_64_aes_launch_end(void);
void x86_64_aes_shell_closed(PD *pd);
void x86_64_aes_launch_process(PD *pd, PD *parent);
void x86_64_aes_drain(void);
BOOL x86_64_aes_owned(PD *pd);
BOOL x86_64_aes_associated(PD *pd);
BOOL x86_64_aes_begin_call(PD *pd);
BOOL x86_64_aes_call_cancelled(PD *pd);
LONG x86_64_aes_wait(PD *pd);
LONG x86_64_aes_yield(PD *pd);
LONG x86_64_aes_events(PD *pd, const AES_EVENT_REQUEST *request,
                       AES_INPUT_EVENT *event, BOOL message_ready);
void x86_64_aes_wake(PD *pd);
void x86_64_aes_flags(PD *pd, UWORD flags);
void x86_64_aes_registration(PD *pd, BOOL active);
LONG x86_64_aes_message(struct aespd *pd, const WORD *message);
void x86_64_aes_pending_message(PD *pd);
#if CONF_WITH_X32_ACCESSORY_TEST
struct _object;
void x86_64_aes_test_begin(void);
BOOL x86_64_aes_test_run(void);
void x86_64_aes_test_finish(void);
void x86_64_aes_test_idle(void);
void x86_64_aes_test_desktop(struct _object *desktop, WORD root);
#endif
#endif
