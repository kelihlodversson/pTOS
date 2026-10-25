/* Retained native FAT backing objects for private file mappings (#660). */
#ifndef FILEBACK_H
#define FILEBACK_H
#include "portab.h"
typedef struct file_backing FILE_BACKING;
LONG fileback_get(LONG handle, FILE_BACKING **out);
void fileback_hold(FILE_BACKING *backing);
void fileback_put(FILE_BACKING *backing);
ULONG fileback_size(const FILE_BACKING *backing);
LONG fileback_read(FILE_BACKING *backing, ULONG offset, void *buffer, ULONG size);
void fileback_changed(void *dfd);
void fileback_reap(void);
LONG fileback_barrier(void);
#endif
