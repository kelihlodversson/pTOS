/* x32rt.h - the heap behind Malloc()/Mfree() (x32rt.c) */
#ifndef X32RT_H
#define X32RT_H
void *x32_malloc(long bytes);
long x32_free(void *p);
#endif
