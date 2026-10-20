/* Typed user-mode marshaling; all shared state stays in the kernel.
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "aes_service.h"

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
