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
/*
 * Builds the ring-3 address space for a process about to be launched:
 * its environment block and its basepage..p_hitpa range, and `parent`'s
 * basepage kernel-only.  FALSE
 * (nothing built, nothing leaked) if memory ran out; the caller reports
 * ENSMEM.  The address space belongs to the KPROC record and is freed
 * with it by kproc_destroy(), exactly once.  kproc_create() must have
 * succeeded for pd first.
 */
BOOL kproc_prepare_user(PD *pd, PD *parent);
void kproc_mark_started(PD *pd);        /* proc_go() launched it */
/* kproc_destroy() that also says whether the record belonged to a basepage
 * that was never launched, whose inherited file and directory references
 * are therefore still held. */
BOOL kproc_discard(PD *pd);
UQUAD kproc_user_pml4(PD *pd);          /* 0 if not prepared */
ULONG kproc_count(void);                /* live records, for leak tests */
BOOL kproc_validate_user_dta(UQUAD address);
BOOL kproc_validate_user_range(UQUAD address, ULONG size);
BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size);
BOOL kproc_copy_to_user(UQUAD address, const void *src, ULONG size);
#endif

#endif
