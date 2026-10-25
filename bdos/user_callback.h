/* Kernel-owned registrations and synchronous, typed user callback delivery. */
#ifndef BDOS_USER_CALLBACK_H
#define BDOS_USER_CALLBACK_H

#include "config.h"
#include "portab.h"
#include "bdosdefs.h"
#include "../include/user_callback.h"

#if CONF_WITH_USER_CALLBACKS
#include "procmem.h"
typedef struct user_callback_state USER_CALLBACK_STATE;
typedef struct user_callback USER_CALLBACK;
enum user_callback_signature {
    USER_CALLBACK_I32_VOID, USER_CALLBACK_I16_VOID,
    USER_CALLBACK_I16_I16_U32, USER_CALLBACK_I16_I16_VOID,
    USER_CALLBACK_DATA_U16, USER_CALLBACK_SIGNATURES
};
typedef union {
    void (*i32_void)(int);
    void (*i16_void)(WORD);
    ULONG (*i16_i16_u32)(WORD, WORD);
    void (*i16_i16_void)(WORD, WORD);
} USER_CALLBACK_NATIVE;

BOOL user_callback_image_load(ASPACE *as);
BOOL user_callback_work_register(void (*work)(void));
BOOL user_callback_active(void);
LONG user_callback_exchange(USER_CALLBACK **slot, UWORD signature,
                             UQUAD handler, const USER_CALLBACK_NATIVE *fallback,
                             ULONG *old);
void user_callback_drop(USER_CALLBACK **slot);
BOOL user_callback_owner(USER_CALLBACK *handler, PD *pd);
BOOL user_callback_owner_failed(PD *pd);
LONG user_callback_invoke(USER_CALLBACK *handler, UWORD signature,
                           LONG a, LONG b, ULONG *result);
LONG user_callback_invoke_data(USER_CALLBACK *handler, const void *data,
                                ULONG bytes, ULONG *result);
void user_callback_process_exit(PD *pd);
BOOL user_callback_pinned(PD *pd, UQUAD address, UQUAD size);
/* Committed syscall returns retain boundary validation but defer optional
 * hooks until a later syscall or user IRQ boundary. */
void user_callback_return_work(UQUAD rsp, BOOL allow_hooks);
BOOL user_callback_fault(void);
BOOL user_callback_syscall_allowed(UQUAD call, UQUAD first);
LONG user_callback_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c,
                            UQUAD return_pc);
#endif
#endif
