/* Private shell lookup state; the caller's DTA is never used for searching. */
#ifndef LIBAES_SHELL_CONTEXT_H
#define LIBAES_SHELL_CONTEXT_H
#include "emutos.h"
#include "bdosdefs.h"
#include "shell_state.h"
typedef struct aes_shell_call {
    char work[AES_SHELL_PATH];
    DTA *dta;
    AES_SHELL_STATE *state;
    void *original_dta;
    BOOL ok;
} SHELL_CONTEXT;
void aes_shell_read_core(SHELL_CONTEXT *aes_shell, char *command, char *tail);
char *aes_shell_name(char *path);
void aes_shell_env_core(SHELL_CONTEXT *aes_shell, char **result, const char *prefix);
WORD aes_shell_find_core(SHELL_CONTEXT *aes_shell, char *path);
BOOL aes_shell_copy(char *dest, const char *src, ULONG size);
BOOL aes_shell_append(char *dest, const char *src, ULONG size);
BOOL aes_shell_path_valid(const char *path, const char *name);
void aes_shell_set_dta(SHELL_CONTEXT *aes_shell, void *dta);
WORD aes_shell_first(SHELL_CONTEXT *aes_shell, char *path, WORD attributes);
#endif
