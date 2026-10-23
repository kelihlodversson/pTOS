/* Typed user-mode marshaling; all shared state stays in the kernel.
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "aes_service.h"
#include "aes_shell.h"

static LONG service(ULONG fn, UQUAD a, UQUAD b)
{
    register UQUAD rax __asm__("rax") = ((UQUAD)AES_SERVICE_CLASS << 32) | fn;
    register UQUAD rdi __asm__("rdi") = a;
    register UQUAD rsi __asm__("rsi") = b;
    register UQUAD rdx __asm__("rdx") = 0;
    register UQUAD r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)rax;
}

LONG aes_appl_init(const char *name, AES_APP_INFO *info)
{
    return service(AES_SERVICE_INIT, (uintptr_t)name, (uintptr_t)info);
}
LONG aes_appl_exit(void)
{
    return service(AES_SERVICE_EXIT, 0, 0);
}
LONG aes_shell(const AES_SHELL_REQUEST *request)
{
    return service(AES_SERVICE_SHELL, (uintptr_t)request, 0);
}
LONG aes_appl_find(const char *name)
{
    return service(AES_SERVICE_FIND, (uintptr_t)name, 0);
}
LONG aes_appl_write(WORD recipient, const AES_MESSAGE *message)
{
    return service(AES_SERVICE_WRITE, (UWORD)recipient, (uintptr_t)message);
}
LONG aes_appl_read(AES_MESSAGE *message)
{
    return service(AES_SERVICE_READ, (uintptr_t)message, 0);
}
LONG aes_evnt_poll(UWORD mask, AES_EVENT *event)
{
    return service(AES_SERVICE_POLL, mask, (uintptr_t)event);
}
LONG aes_wind_update(WORD code)
{
    return service(AES_SERVICE_UPDATE, (UWORD)code, 0);
}

LONG aes_evnt_wait(UWORD mask, AES_EVENT *event)
{
    return service(AES_SERVICE_WAIT, mask, (uintptr_t)event);
}
LONG aes_appl_yield(void)
{
    return service(AES_SERVICE_YIELD, 0, 0);
}
LONG aes_evnt_multi(const AES_EVENT_REQUEST *request, AES_INPUT_EVENT *event)
{
    return service(AES_SERVICE_MULTI, (uintptr_t)request, (uintptr_t)event);
}

LONG aes_wind_create(UWORD kind, const GRECT *full)
{
    AES_WINDOW_REQUEST r = { AES_WIN_CREATE, 0, 0, kind, 0, 0,
                             (ULONG)(uintptr_t)full, { 0 } };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_open(WORD handle, const GRECT *rectangle)
{
    AES_WINDOW_REQUEST r = { AES_WIN_OPEN, handle, 0, 0, 0, 0,
                             (ULONG)(uintptr_t)rectangle, { 0 } };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_close(WORD handle)
{
    AES_WINDOW_REQUEST r = { AES_WIN_CLOSE, handle };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_delete(WORD handle)
{
    AES_WINDOW_REQUEST r = { AES_WIN_DELETE, handle };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_get(WORD handle, AES_WINDOW_INFO *info)
{
    AES_WINDOW_REQUEST r = { AES_WIN_GET, handle };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, (uintptr_t)info);
}
LONG aes_wind_set_rect(WORD handle, const GRECT *rectangle)
{
    AES_WINDOW_REQUEST r = { AES_WIN_RECT, handle, 0, 0, 0, 0,
                             (ULONG)(uintptr_t)rectangle, { 0 } };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_set_text(WORD handle, UWORD field, const char *text)
{
    AES_WINDOW_REQUEST r = { AES_WIN_TEXT, handle, field, 0, 0, 0,
                             (ULONG)(uintptr_t)text, { 0 } };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_top(WORD handle)
{
    AES_WINDOW_REQUEST r = { AES_WIN_TOP, handle };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_slider(WORD handle, UWORD field, WORD value)
{
    AES_WINDOW_REQUEST r = { AES_WIN_SLIDER, handle, field, 0, value };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
LONG aes_wind_rect(WORD handle, UWORD next, GRECT *rectangle)
{
    AES_WINDOW_REQUEST r = { AES_WIN_VISIBLE, handle, next };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, (uintptr_t)rectangle);
}
LONG aes_wind_calc(UWORD mode, UWORD kind, const GRECT *input, GRECT *output)
{
    AES_WINDOW_REQUEST r = { AES_WIN_CALC, 0, mode, kind, 0, 0,
                             (ULONG)(uintptr_t)input, { 0 } };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, (uintptr_t)output);
}
LONG aes_wind_find(WORD x, WORD y)
{
    AES_WINDOW_REQUEST r = { AES_WIN_FIND, 0, 0, 0, 0, 0, 0, { x, y, 0, 0 } };
    return service(AES_SERVICE_WINDOW, (uintptr_t)&r, 0);
}
