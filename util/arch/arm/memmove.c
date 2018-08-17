/*
 * memmove_c.c - simple c implementation of memove and memcpy
 *
 * Copyright (C) 2002-2016 The EmuTOS development team
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
#define REPEAT_BYTE(c) (0x01010101 * (c))

typedef union { char* b ; machine_word_t* w; int_ptr_t i; const void* v;} ptr_t;

void* memmove(void* in_dst, const void* in_src, size_t length)
{
    ptr_t dst;
    ptr_t src;

    if(in_src == in_dst || length == 0)
        return in_dst;

    // move up
    if( in_src > in_dst || (in_src + length) < in_dst)
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
