/* Internal Pclone entry and GEMDOS argument dispatch. */
#ifndef CLONE_H
#define CLONE_H

#include <stdint.h>

long xclone(uint64_t address, uint64_t size);

#endif
