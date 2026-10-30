/* Private shell lookup state; the caller's DTA is never used for searching. */
#ifndef LIBAES_SHELL_CONTEXT_H
#define LIBAES_SHELL_CONTEXT_H
#include "emutos.h"
#include "bdosdefs.h"
#define AES_SHELL_PATH 260
#define AES_SHELL_ENV 32768UL
typedef struct {
    char work[AES_SHELL_PATH];
    DTA dta;
    void *original_dta;
    BOOL ok;
} SHELL_CONTEXT;
extern SHELL_CONTEXT *aes_shell;
extern char aes_shell_command[AES_SHELL_PATH], aes_shell_tail[128];
extern char *aes_shell_environment;
void aes_library_init(void);
void aes_shell_read_core(char *command, char *tail);
char *aes_shell_name(char *path);
void aes_shell_env_core(char **result, const char *prefix);
WORD aes_shell_find_core(char *path);
BOOL aes_shell_copy(char *dest, const char *src, ULONG size);
BOOL aes_shell_append(char *dest, const char *src, ULONG size);
BOOL aes_shell_path_valid(const char *path, const char *name);
void aes_shell_set_dta(void *dta);
WORD aes_shell_first(char *path, WORD attributes);
#endif
