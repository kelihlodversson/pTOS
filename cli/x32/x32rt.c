/*
 * x32rt.c - run-time support for the x32 (ring 3) EmuCON
 *
 * What libcmini gives the m68k and ARM builds, for the one program that
 * has no libcmini yet: the process entry, the basepage pointer, a few
 * string functions and a small heap.  The rest of the C library EmuCON
 * needs (strcpy(), sprintf(), ...) is util/string.c and util/doprintf.c,
 * built for x32 as well.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include <stddef.h>
#include <mint/osbind.h>
#include <mint/basepage.h>
#include "x32rt.h"

struct basepage *_base;

/* The kernel's PD (include/bdosdefs.h) puts p_env at offset 0x2c. */
typedef char p_env_offset_is_0x2c[(__builtin_offsetof(struct basepage, p_env) == 0x2c) ? 1 : -1];

int main(void);

/* Entered from x32crt.S with the process-entry contract's registers. */
void x32_start(struct basepage *bp, long entry_type) __attribute__((noreturn));

void x32_start(struct basepage *bp, long entry_type)
{
    int rc;

    (void)entry_type;           /* ENTRY_PROGRAM: nothing else to select */
    _base = bp;
    rc = main();
    Pterm(rc);
    for (;;)
        ;
}

/* ---- memory functions ---------------------------------------------- */

/*
 * Plain byte loops.  Built with -fno-tree-loop-distribute-patterns and
 * -fno-builtin so the compiler cannot turn them back into calls to
 * themselves.
 */
void *memset(void *p, int c, size_t n)
{
    unsigned char *d = p;

    while (n--)
        *d++ = (unsigned char)c;
    return p;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d < s || d >= s + n) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

/* ---- heap -----------------------------------------------------------
 *
 * Malloc() and Mfree() are not system calls here: the kernel's allocator
 * hands out memory from its own pools, which are not user memory.  A
 * program that needs a few buffers (EmuCON's line history, its copy
 * buffer) gets them from a static arena instead: first fit over a free
 * list, blocks coalescing with their neighbours when freed.
 */
#define HEAP_BYTES   (192 * 1024)
#define ALIGN        16

struct block {
    unsigned long size;         /* payload bytes; bit 0 set: in use */
    struct block *next;         /* next block in address order */
};

#define HDR     ((sizeof(struct block) + ALIGN - 1) & ~(size_t)(ALIGN - 1))

static unsigned char heap[HEAP_BYTES] __attribute__((aligned(ALIGN)));
static struct block *head;

void *x32_malloc(long bytes)
{
    struct block *b;
    size_t want;

    if (!head) {
        head = (struct block *)heap;
        head->size = HEAP_BYTES - HDR;
        head->next = NULL;
    }
    if (bytes <= 0 || (size_t)bytes > HEAP_BYTES)
        return NULL;
    want = ((size_t)bytes + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    for (b = head; b; b = b->next) {
        if ((b->size & 1) || b->size < want)
            continue;
        if (b->size >= want + HDR + ALIGN) {            /* split */
            struct block *rest = (struct block *)((unsigned char *)b + HDR + want);

            rest->size = b->size - want - HDR;
            rest->next = b->next;
            b->next = rest;
            b->size = want;
        }
        b->size |= 1;
        return (unsigned char *)b + HDR;
    }
    return NULL;
}

long x32_free(void *p)
{
    struct block *b, *c;

    if (!p)
        return 0;
    for (b = head; b; b = b->next)
        if ((unsigned char *)b + HDR == (unsigned char *)p)
            break;
    if (!b || !(b->size & 1))
        return -40;                     /* EIMBA: not a live block */
    b->size &= ~1UL;
    for (c = head; c; c = c->next)      /* coalesce with free neighbours */
        while (!(c->size & 1) && c->next && !(c->next->size & 1)) {
            c->size += HDR + c->next->size;
            c->next = c->next->next;
        }
    return 0;
}
