/* string.h - the few string functions EmuCON uses (util/string.c, x32rt.c) */
#ifndef X32_STRING_H
#define X32_STRING_H
#include <stddef.h>

size_t strlen(const char *s);
char *strcpy(char *d, const char *s);
int strncmp(const char *a, const char *b, size_t n);
int strncasecmp(const char *a, const char *b, size_t n);
void *memset(void *p, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);

#endif
