/* Saved launch metadata owned by one userspace AES context. */
#ifndef LIBAES_SHELL_STATE_H
#define LIBAES_SHELL_STATE_H
#include "portab.h"
#include "bdosdefs.h"
#define AES_SHELL_PATH 260
#define AES_SHELL_ENV 32768UL
typedef struct shell_search {
    DTA dta;
    struct shell_search *next;
    BOOL busy;
} SHELL_SEARCH;
typedef struct {
    SHELL_SEARCH search;
    char command[AES_SHELL_PATH], tail[128], directory[AES_SHELL_PATH];
    char *environment;
    ULONG environment_bytes;
    BOOL startup_ready, capture_pending;
    char empty_environment[2];
} AES_SHELL_STATE;
#endif
