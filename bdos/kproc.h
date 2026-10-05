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
#include "procmem.h"      /* X86_64_ASPACE (x86-64 only; empty elsewhere) */
#include "x32image.h"

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
/* The launching process, recorded by proc_go() and used by Pterm() instead
 * of the user-writable p_parent field.  kproc_get_parent() halts if there
 * is no record (like kproc_get_dta()). */
void kproc_set_parent(PD *pd, PD *parent);
PD *kproc_get_parent(PD *pd);
void kproc_mark_started(PD *pd);        /* proc_go() launched it */
/* kproc_destroy() that also says whether the record belonged to a basepage
 * that was never launched, whose inherited file and directory references
 * are therefore still held. */
BOOL kproc_discard(PD *pd);
/*
 * Makes the process a built-in x32 program: kproc_prepare_user() will also
 * map the image's segments as private pages of its address space.  Must
 * come after kproc_create() and before the launch; FALSE if the image does
 * not pass x86_64_x32image_check() or the address space already exists.
 * The entry point is then the image's own, kept in the KPROC record and
 * never read back from the user-writable p_tbase.
 */
BOOL kproc_set_image(PD *pd, const X32_IMAGE *image);
UQUAD kproc_ancestors_va(PD *pd);       /* where its basepage copies are; 0 if none */
UQUAD kproc_user_entry(PD *pd);         /* image entry point; 0 if none */
/* Hands the process's kernel stack (the one its system calls run on) to the
 * launcher, which frees it with x86_64_kstack_free() once the process has
 * exited and the launcher runs on its own stack again: the process's last
 * system call, Pterm(), is still using it while the record is destroyed.
 * Returns the physical base (0 if none) and the initial stack pointer. */
/*
 * A ring-3 process launching a child (Pexec() from its own system call)
 * builds the child's blocks under its own page tables.  kproc_borrow() maps a
 * newly allocated block into the launcher's address space, supervisor-only
 * (FALSE if that fails: the caller frees the block); a ring-0 launcher needs
 * nothing.  kproc_unborrow() takes it away again, and must run before the
 * block is freed.
 */
BOOL kproc_borrow(PD *launcher, void *block);
/* Pexec() from ring 3: who made a basepage, handing its blocks to that
 * caller, and checking a basepage it asks to launch (E_OK, EIMBA, EPLFMT). */
void kproc_set_creator(PD *pd, PD *creator);
void kproc_hand_over(PD *caller, PD *child);
LONG kproc_check_launch(PD *pd, PD *caller);
void kproc_unborrow(void *block);
ULONG kproc_borrow_count(PD *launcher, void *block);
UQUAD kproc_take_kernel_stack(PD *pd, UQUAD *top);
UQUAD kproc_user_stack(PD *pd);         /* initial RSP of an image's own stack; 0 if none */
UQUAD kproc_user_pml4(PD *pd);          /* 0 if not prepared */
X86_64_ASPACE *kproc_user_aspace(PD *pd);   /* NULL if none (tests) */
ULONG kproc_count(void);                /* live records, for leak tests */
BOOL kproc_validate_user_dta(UQUAD address);
/* valid user memory of the current process: mapped, user-accessible and (for
 * _write) writable in its own address space */
BOOL kproc_validate_user_range(UQUAD address, ULONG size);
BOOL kproc_validate_user_write(UQUAD address, ULONG size);
BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size);
BOOL kproc_copy_to_user(UQUAD address, const void *src, ULONG size);
#endif

#endif
