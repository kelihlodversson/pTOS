/* Launcher-owned kernel entry arguments, copied before user RTE. */
#ifndef M68K_USERENTRY_H
#define M68K_USERENTRY_H
#define M68K_ENTRY_PC 0
#define M68K_ENTRY_USP 4
#define M68K_ENTRY_SSP 8
#define M68K_ENTRY_ARG 12
#ifndef ASM_SOURCE
#include "portab.h"
#include <stddef.h>
typedef struct {
    ULONG entry, user_sp, kstack_top, argument;
} M68K_USER_ENTRY;
typedef char m68k_user_entry_layout[sizeof(M68K_USER_ENTRY)==16 &&
    offsetof(M68K_USER_ENTRY,entry)==M68K_ENTRY_PC &&
    offsetof(M68K_USER_ENTRY,user_sp)==M68K_ENTRY_USP &&
    offsetof(M68K_USER_ENTRY,kstack_top)==M68K_ENTRY_SSP &&
    offsetof(M68K_USER_ENTRY,argument)==M68K_ENTRY_ARG ? 1:-1];
void m68k_enter_user(const M68K_USER_ENTRY *entry) NORETURN;
#endif
#endif
