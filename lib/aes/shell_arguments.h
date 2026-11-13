/* Private preparation of standard GEMDOS tails and ARGV environments. */
#ifndef LIBAES_SHELL_ARGUMENTS_H
#define LIBAES_SHELL_ARGUMENTS_H
#include "libaes.h"
typedef struct {
    char tail[128];
    char *environment;
} SHELL_ARGUMENTS;
BOOL aes_shell_arguments(const char *path, const char *tail, BOOL parse,
                         const char *environment, ULONG limit, SHELL_ARGUMENTS *out, AESGLOBAL *global);
#endif
