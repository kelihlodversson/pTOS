/*
 * isoprobe.c - the program the isolation suite (isolation.c) launches (#572)
 *
 * A standalone TOS program, not a ptest suite: it does the one thing the
 * first character of its command tail names, at the address after it, and
 * says what became of it.  The suite starts it once per forbidden access.
 *
 *     0            exit with 42 (a program enters user mode and leaves it)
 *     r <addr>     read a long word at the address
 *     w <addr>     write a long word there
 *     x <addr>     jump to it
 *     p            execute an instruction only the supervisor may
 *     c <a> <v>    read a long word at <a>; 99 if it is <v>, a value another
 *                  process put there that this one must not see
 *     s <a> <v>    store <v> at <a>
 *
 * An access that is allowed returns 7 (SURVIVED).  One that is not is a fault
 * of this process, which the kernel ends: the exit code the suite sees is
 * the kernel's, and nothing after the access runs.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include <mint/osbind.h>
#include <mint/basepage.h>
#include "include/mmapcall.h"

#define SURVIVED 7
#define BROKEN   99

static unsigned long hex(const char **p)
{
    unsigned long v = 0;

    while (**p == ' ')
        (*p)++;
    for (;; (*p)++)
    {
        char c = **p;

        if (c >= '0' && c <= '9')
            v = v << 4 | (unsigned long)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = v << 4 | (unsigned long)(c - 'a' + 10);
        else
            return v;
    }
}


#if defined(__mc68000__) || defined(__arm__)
/*
 * The address-translation cache and the context switch (#737): the parent
 * has a page of its own at an address, and so has each of these probes; what
 * each sees there is its own.  The calls that change a mapping the CPU has
 * already used (a warm entry) must take effect at once.
 */
#define RW (PROT_READ | PROT_WRITE)

/* a page of our own at <a>: it is new (zeros, not the page of another process there), and holds what is written */
static int own_page(unsigned long a, unsigned long v)
{
    volatile unsigned long *q = (volatile unsigned long *)a;

    if (mm_map((long)a, 1, RW, 1) != (long)a)
        return 0;
    if (*q != 0)
        return 0;
    *q = v;
    return *q == v;
}
#endif

int main(void)
{
    /* the tail is a Pascal string: its length, then the text */
    const char *t = (const char *)_base->p_cmdlin + 1;
    char op = t[0];
    unsigned long a, v;
    volatile unsigned long *p;

    t++;
    a = hex(&t);
    v = hex(&t);
    p = (volatile unsigned long *)a;

    switch (op)
    {
    case '0':
        return 42;
    case 'r':
        v = *p;
        break;
    case 'w':
        *p = 0x5a5a5a5aUL;
        break;
    case 'x':
        ((void (*)(void))a)();
        break;
    case 'p':
#if defined(__arm__)
        __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (v));
#elif defined(__mc68000__)
        __asm__ volatile ("reset");
#endif
        break;
    case 'c':
        if (*p == v)
            return BROKEN;
        break;
    case 's':
        *p = v;
        break;
#if defined(__mc68000__) || defined(__arm__)
    case 'M':           /* a page of our own */
        return own_page(a, v) ? SURVIVED : BROKEN;
    case 'R':           /* ... unmapped and mapped again: new zeros, not what the CPU remembers */
        if (!own_page(a, v))
            return BROKEN;
        if (mm_unmap((long)a, 1) != 0)
            return BROKEN;
        return own_page(a, v + 1) ? SURVIVED : BROKEN;
    case 'D':           /* permissions down and up again, with the page in the cache between */
        if (!own_page(a, v))
            return BROKEN;
        if (mm_prot((long)a, 1, PROT_READ) != 0 || *p != v)
            return BROKEN;
        if (mm_prot((long)a, 1, RW) != 0)
            return BROKEN;
        *p = v + 1;
        if (*p != v + 1 || mm_prot((long)a, 1, PROT_READ) != 0 || mm_prot((long)a, 1, RW) != 0)
            return BROKEN;
        *p = v + 2;
        return *p == v + 2 ? SURVIVED : BROKEN;
    case 'W':           /* read-only after a write: the next write must fault */
        if (!own_page(a, v))
            return BROKEN;
        if (mm_prot((long)a, 1, PROT_READ) != 0)
            return BROKEN;
        *p = v + 1;
        break;
    case 'N':           /* no access after a write: the next read must fault */
        if (!own_page(a, v))
            return BROKEN;
        if (mm_prot((long)a, 1, PROT_NONE) != 0)
            return BROKEN;
        v = *p;
        break;
    case 'U':           /* unmapped after a write: the next read must fault */
        if (!own_page(a, v))
            return BROKEN;
        if (mm_unmap((long)a, 1) != 0)
            return BROKEN;
        v = *p;
        break;
#endif
    default:
        return 1;
    }
    return SURVIVED;
}
