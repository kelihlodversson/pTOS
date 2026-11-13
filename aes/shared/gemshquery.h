/* Native AES startup/environment/search core, with optional user bindings. */
#include "aescontext.h"
#ifndef GEMSHQUERY_H
#define GEMSHQUERY_H
void sh_read(AES_SHELL_PARAM char *command, char *tail);
char *sh_name(char *path);
void sh_envrn(AES_SHELL_PARAM char **result, const char *prefix);
WORD sh_find(AES_SHELL_PARAM char *path);
#endif
