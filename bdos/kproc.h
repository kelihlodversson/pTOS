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
#if CONF_WITH_USER_ASPACE
#include "procmem.h"      /* X86_64_ASPACE */
#include "x32image.h"
#endif

typedef struct dta_info DTAINFO;

BOOL kproc_create(PD *pd);
void kproc_destroy(PD *pd);
void kproc_set_dta(PD *pd, DTAINFO *dta);
DTAINFO *kproc_get_dta(PD *pd);
#if CONF_WITH_KPROC
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
BOOL kproc_unlaunched(PD *pd);          /* has a record and was never launched */
#endif
#if CONF_WITH_USER_ASPACE
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
/*
 * Makes the process a built-in x32 program: kproc_prepare_user() will also
 * map the image's segments as private pages of its address space.  Must
 * come after kproc_create() and before the launch; FALSE if the image does
 * not pass x86_64_x32image_check() or the address space already exists.
 * The entry point is then the image's own, kept in the KPROC record and
 * never read back from the user-writable p_tbase.
 */
BOOL kproc_set_image(PD *pd, const X32_IMAGE *image);
/* A program Pexec() loads from a file (bdos/elfld.c) is read straight into pages
 * of the address space that will be the new process's: kproc_load_begin() makes
 * it and maps [va, va + bytes) in it, the image and the startup area after it
 * (X86_64_USER_IMAGE_SLACK); kproc_load_enter() makes it the one in use while
 * the loader fills the pages, kproc_load_leave() puts the previous one back (and
 * destroying the process does too, for a disk error that jumps past the loader).
 * kproc_set_loaded_image() then says where the segments are (`layout`, already
 * relocated if it had to be), and gives each its permissions.  The launch adopts
 * the address space.  If the launch fails after that the program is gone, and
 * the basepage can only be freed. */
BOOL kproc_load_begin(PD *pd, UQUAD va, UQUAD bytes);
void kproc_load_enter(PD *pd);
void kproc_load_leave(void);
BOOL kproc_set_loaded_image(PD *pd, const X32_LAYOUT *layout);
/*
 * Pexec(PE_LOAD) from a ring-3 caller loads the program into the caller's own
 * address space, in its heap, where it can read and patch it before the
 * launch: kproc_load_alloc() takes the block for the process `child` that is
 * being made and records it with the child, so that destroying the child, a
 * failed load included, gives it back (0: none), the loader reads the file into it, in place, and
 * kproc_set_moved_image() makes it the child's image, laid out as `layout`.
 * Nothing is mapped for the child yet: PE_GO moves the block's pages, at the
 * same addresses, from the caller's address space to the child's, with each
 * segment's permissions.  The caller cannot free the block (Mfree() leaves it
 * alone); freeing the child's basepage unlaunched gives it back, and so does
 * kproc_load_release().  With PE_GOTHENFREE the pages stay with the child;
 * with PE_GO (kproc_set_give_back()) they only are lent, and move back to the
 * caller as Malloc() memory when the child ends.
 */
UQUAD kproc_load_alloc(PD *caller, PD *child, ULONG bytes);
void kproc_load_release(PD *caller, UQUAD va);
BOOL kproc_set_moved_image(PD *pd, const X32_LAYOUT *layout, PD *caller, UQUAD va, ULONG pages);
/* PE_GO (not PE_GOTHENFREE): the image goes back to the caller's heap when the
 * process ends, Pterm() or Ptermres(); call it before kproc_prepare_user(). */
void kproc_set_give_back(PD *pd, BOOL give_back);
/* Fsfirst()/Fsnext() search state kept kernel-side: kproc_dta_save() after a
 * search, kproc_dta_restore() before Fsnext() (FALSE: no search was made with
 * this DTA, the search is over). */
BOOL kproc_dta_restore(PD *pd, DTAINFO *dta);
void kproc_dta_save(PD *pd, const DTAINFO *dta);
void kproc_dta_forget(PD *pd, const DTAINFO *dta);   /* a new Fsfirst() starts */
UQUAD kproc_ancestors_va(PD *pd);       /* where its basepage copies are; 0 if none */
UQUAD kproc_user_entry(PD *pd);         /* image entry point; 0 if no image (it starts at p_tbase) */
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
/*
 * Malloc() memory of a process with an address space: private pages in
 * [X86_64_USER_HEAP_BASE, X86_64_USER_HEAP_LIMIT), freed with the process.
 * Alloc returns the address of `bytes` rounded up to whole pages (0: none),
 * largest what Malloc(-1) reports; free and shrink are Mfree() and
 * Mshrink() of a block's start (EIMBA: no such block, EGSBF: bigger).
 */
BOOL kproc_has_heap(PD *pd);
UQUAD kproc_uheap_alloc(PD *pd, ULONG bytes);
ULONG kproc_uheap_largest(PD *pd);
LONG kproc_uheap_free(PD *pd, UQUAD va);
LONG kproc_uheap_shrink(PD *pd, UQUAD va, long len);
UQUAD kproc_user_stack(PD *pd);         /* initial RSP of the private stack of a prepared process; 0 if not prepared */
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
