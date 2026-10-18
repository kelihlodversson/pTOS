/*
 * memmove.c - simple c implementation of memmove, memcpy, memset and bzero
 *
 * Identical to util/arch/arm/memmove.c -- deliberately duplicated, not
 * shared via a generic (non-arch-dir) location: memmove.o is unconditional
 * obj-y (util/build.mk), and GNU Make picks its %.o: %.c pattern rule the
 * moment vpath resolves *any* memmove.c, before ever trying the %.o: %.S
 * rule -- regardless of which vpath directory matched, or how far down
 * the (machine, then arch, then generic) search order it was found. A
 * generic util/memmove.c therefore pre-empts m68k's own
 * util/arch/m68k/memmove.S build-wide, not just where nothing more
 * specific exists, colliding with util/arch/m68k/memset.S's own
 * memset()/bzero_nobuiltin() (multiple definition). Two arch-dir copies
 * avoids the trap entirely, since m68k's own build never has this file
 * in its vpath search path at all.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#include "config.h"
#include "portab.h"
#include "string.h"
/* moves length bytes from src to dst. returns dst as passed.
 * the behaviour is undefined if the two regions overlap.
 */
void* memcpy(void* dst, const void* src, size_t length)
{
    return memmove(dst, src, length);
}

/* moves length bytes from src to dst, performing correctly
 * if the two regions overlap. returns dst as passed.
 */

typedef unsigned long machine_word_t;
typedef unsigned long int_ptr_t;
#define STEP_SIZE sizeof(machine_word_t)
#define ALIGN_MASK (STEP_SIZE-1)
/*
 * machine_word_t is 8 bytes here (unlike the identical-looking ARM copy
 * of this file, where "unsigned long" is 4 bytes): a fixed 0x01010101
 * pattern only fills the low 4 bytes of each word, leaving the high 4
 * zeroed and corrupting every memset() fill of more than STEP_SIZE
 * bytes with a nonzero c. (~0 / 0xff) instead produces a pattern of
 * 0x01 bytes exactly as wide as machine_word_t, whatever that width is.
 */
#define REPEAT_BYTE(c) ((machine_word_t)(unsigned char)(c) * (machine_word_t)(~(machine_word_t)0 / 0xff))

typedef union { char* b ; machine_word_t* w; int_ptr_t i; const void* v;} ptr_t;

void* memmove(void* in_dst, const void* in_src, size_t length)
{
    ptr_t dst;
    ptr_t src;

    if(in_src == in_dst || length == 0)
        return in_dst;

    // move up
    /* Compared as integer addresses, not as pointers: memmove()/memcpy()
     * must support buffers from two entirely unrelated objects, but C
     * only defines `<`/`>` between pointers into the same array/object
     * -- comparing across two unrelated allocations is undefined
     * behavior, and can be optimized into the wrong copy direction.
     * The alignment check just below already goes through ptr_t's own
     * `.i` (int_ptr_t) member for the identical reason; do the same
     * here instead of comparing in_src/in_dst directly. */
    if ((int_ptr_t)in_src > (int_ptr_t)in_dst
        || (int_ptr_t)in_src + length < (int_ptr_t)in_dst)
    {
        dst.v = in_dst;
        src.v = in_src;
        int remainder = length;
        if ( ((src.i ^ dst.i) & ALIGN_MASK) == 0 )
        {
            while((dst.i & ALIGN_MASK))
            {
                *(dst.b++) = *(src.b++);
                if(--length == 0) return in_dst;
            }

            int word_count = length / STEP_SIZE;
            remainder = length % STEP_SIZE;

            while(word_count--)
            {
                *(dst.w++) = *(src.w++);
            }

        }
        while(remainder--)
        {
            *dst.b++ = *src.b++;
        }
    }
    // src and dst ovelap: copy backwards
    else
    {
        dst.v = in_dst + length;
        src.v = in_src + length;
        int remainder = length;
        if ( ((src.i ^ dst.i) & ALIGN_MASK) == 0 )
        {
            while((dst.i & ALIGN_MASK))
            {
                *--dst.b = *--src.b;
                if(--length == 0) return in_dst;
            }

            int word_count = length / STEP_SIZE;
            remainder = length % STEP_SIZE;

            while(word_count--)
            {
                *(--dst.w) = *(--src.w);
            }

        }
        while(remainder--)
        {
            *--dst.b = *--src.b;
        }

    }

    return in_dst;
}


/* fills with byte c, returns the given address. */
void * memset(void *address, int c, size_t length)
{
    ptr_t dst;
    machine_word_t pattern = REPEAT_BYTE(c);

    if(length)
    {
        dst.v = address;
        while((dst.i & ALIGN_MASK))
        {
            *(dst.b++) = (char)c;
            if(--length == 0) return address;
        }

        int word_count = length / STEP_SIZE;
        int remainder = length % STEP_SIZE;

        while(word_count--)
        {
            *(dst.w++) = pattern;
        }

        while(remainder--)
        {
            *dst.b++ = (char)c;
        }
    }

    return address;
}

void bzero(void *address, size_t size)
{
    memset(address, 0, size);
}
