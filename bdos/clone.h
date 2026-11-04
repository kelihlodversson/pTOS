/* Internal Pclone entry and GEMDOS argument dispatch. */
#ifndef CLONE_H
#define CLONE_H

#include <stdint.h>

long xclone(uint64_t address, uint64_t size);
long clone_dispatch_native(const long *frame);
long clone_dispatch_m68k(const int16_t *frame);

#endif
