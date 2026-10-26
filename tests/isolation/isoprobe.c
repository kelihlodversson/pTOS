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
    default:
        return 1;
    }
    return SURVIVED;
}
