#ifndef ELF_INTERP_H
#define ELF_INTERP_H
#include "portab.h"
/* 1: no PT_INTERP (take the static loader); 0: loaded; negative GEMDOS error. */
struct dl_start_vectors;
LONG elf_interp_load(FH file, PD *pd, const char *filename,
                     const struct dl_start_vectors *vectors);
#endif
