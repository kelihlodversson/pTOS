/* Native AES startup/environment/search core, with optional user bindings. */
#ifndef GEMSHQUERY_H
#define GEMSHQUERY_H
void sh_read(char *command, char *tail);
char *sh_name(char *path);
void sh_envrn(char **result, const char *prefix);
WORD sh_find(char *path);
#endif
