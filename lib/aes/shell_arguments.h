/* Translate the standard GEM shell inputs to vectors. */
#ifndef LIBAES_SHELL_ARGUMENTS_H
#define LIBAES_SHELL_ARGUMENTS_H
#include "libaes.h"
typedef struct {
    const char *const *argv, *const *envp;
    void *storage;
    char classic_tail[128];
} SHELL_ARGUMENTS;
BOOL aes_shell_arguments(const char *path, const char *tail, BOOL parse,
                         const char *environment, SHELL_ARGUMENTS *out, AESGLOBAL *global);
const char *const *aes_shell_environ(void);
#endif
