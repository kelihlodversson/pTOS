/* Kernel-private boundary for native x32 AES services.
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef AES_SERVICE_H
#define AES_SERVICE_H
#include "aes_client.h"
#define AES_SERVICE_CLASS 0x101
#define AES_SERVICE_INIT 0
#define AES_SERVICE_EXIT 1
#define AES_SERVICE_FIND 2
#define AES_SERVICE_WRITE 3
#define AES_SERVICE_READ 4
#define AES_SERVICE_POLL 5
#define AES_SERVICE_UPDATE 6
#define AES_SERVICE_WAIT 7
#define AES_SERVICE_YIELD 8
#define AES_SERVICE_MULTI 9
#define AES_SERVICE_WINDOW 10
#define AES_SERVICE_OBJECT 11
#define AES_SERVICE_SHELL 12
#define AES_WIN_CREATE 1
#define AES_WIN_OPEN 2
#define AES_WIN_CLOSE 3
#define AES_WIN_DELETE 4
#define AES_WIN_GET 5
#define AES_WIN_RECT 6
#define AES_WIN_TEXT 7
#define AES_WIN_TOP 8
#define AES_WIN_SLIDER 9
#define AES_WIN_VISIBLE 10
#define AES_WIN_CALC 11
#define AES_WIN_FIND 12
/* Private wire request: data is an x32 address copied by the service, never
 * a native pointer retained by the window manager. rectangle is only used
 * by FIND on the wire; other geometry is copied from data into it. */
typedef struct {
    UWORD operation;
    WORD handle;
    UWORD selector, kind;
    WORD value;
    UWORD reserved;
    ULONG data;
    GRECT rectangle;
} AES_WINDOW_REQUEST;
typedef char aes_window_request_size[(sizeof(AES_WINDOW_REQUEST) == 24) ? 1 : -1];
#if defined(ARCH_X86_64) && !defined(__ILP32__)
#include "bdosdefs.h"
typedef struct aes_client_state AES_CLIENT_STATE;
/* The dispatcher initializes this invocation's return policy to FALSE.
 * Successful cursor copyout sets it to defer optional return-work hooks. */
LONG aes_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d,
                   BOOL *defer_return_work);
void aes_service_process_exit(PD *pd);
LONG aes_service_kernel_message(PD *pd, const WORD *message);
BOOL aes_service_window_unlocked(PD *pd);
/* Called without yielding after validating a registration-scoped token. */
LONG aes_service_update(PD *pd, UWORD code);
LONG x86_64_aes_shell(UQUAD address);
#if CONF_WITH_X32_ACCESSORY_TEST
LONG aes_service_test_coordination(WORD code);
#endif
#endif
#endif
