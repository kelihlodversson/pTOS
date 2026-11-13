/* Private application/session state, owned through the supplied AESGLOBAL.
 * Public AESGLOBAL fields are never copied or shadowed here. */
#ifndef LIBAES_APP_CONTEXT_H
#define LIBAES_APP_CONTEXT_H
#include "aesglobal.h"
#include "graphics_state.h"
#include "window_state.h"
#include "desktop_state.h"
#include "resource_state.h"
#include "menu_state.h"
#include "shell_state.h"
typedef struct {
    BOOL accessory, coordinator, reset_wait_display;
    ULONG stale_window_bytes;
    WORD capabilities;
    LONG reset_token, request_token, owner_token;
} AES_LIFECYCLE_STATE;
typedef struct aes_app_context {
    LONG session, search_after;
    AES_LIFECYCLE_STATE lifecycle;
    AES_GRAPHICS_STATE graphics;
    AES_WINDOW_STATE windows;
    AES_DESKTOP_STATE desktop;
    AES_RESOURCE_STATE resources;
    AES_MENU_STATE menu;
    AES_SHELL_STATE shell;
} AES_APP_CONTEXT;

void aes_shell_init(AES_APP_CONTEXT *context);
AES_SHELL_STATE *aes_shell_state(AESGLOBAL *global);
void aes_shell_reset(AESGLOBAL *global);

/* A live context is a caller precondition, just as for other heap objects.
 * There is no process/thread lookup or validation of arbitrary pointers. */
static __inline__ AES_APP_CONTEXT *aes_app_context(AESGLOBAL *global)
{
    return (AES_APP_CONTEXT *)global->ap_3resv;
}
#endif
