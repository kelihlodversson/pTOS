/*
 * kproc.h - kernel-private process state
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef KPROC_H
#define KPROC_H

#include "bdosdefs.h"

typedef struct dta_info DTAINFO;

BOOL kproc_create(PD *pd);
void kproc_destroy(PD *pd);
void kproc_set_dta(PD *pd, DTAINFO *dta);
DTAINFO *kproc_get_dta(PD *pd);
#ifdef __x86_64__
BOOL kproc_validate_user_dta(UQUAD address);
BOOL kproc_validate_user_range(UQUAD address, ULONG size);
BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size);
BOOL kproc_copy_to_user(UQUAD address, const void *src, ULONG size);
#endif

#endif
