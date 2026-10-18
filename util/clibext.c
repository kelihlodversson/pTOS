/*
 * clibext.c - C library routines that only FreeType needs
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * FreeType (CONF_WITH_VDI_SCALABLE_FONTS, see vdi/freetype/ptos_ftstdlib.h)
 * uses a few standard routines that the rest of pTOS has no use for, so
 * they live here and are only built with it.
 */

#include "emutos.h"
#include "string.h"
#include "stdlib.h"

char *strncpy(char *RESTRICT dest, const char *RESTRICT src, size_t n)
{
    char *d = dest;

    while (n && *src)
    {
        *d++ = *src++;
        n--;
    }
    while (n--)
        *d++ = '\0';

    return dest;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;

    for (;; s++)
    {
        if (*s == (char)c)
            last = s;
        if (!*s)
            break;
    }

    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);

    if (!n)
        return (char *)haystack;

    for (; *haystack; haystack++)
        if (*haystack == *needle && !strncmp(haystack, needle, n))
            return (char *)haystack;

    return NULL;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;

    for (; n; n--, p++)
        if (*p == (unsigned char)c)
            return (void *)p;

    return NULL;
}

/*
 * a Shell sort: FreeType sorts small tables (kerning pairs, glyph names),
 * so this is simple rather than quick, and needs no extra memory
 */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    char *b = base;
    size_t gap, i, j, k;
    char *p, *q, t;

    for (gap = nmemb / 2; gap > 0; gap /= 2)
    {
        for (i = gap; i < nmemb; i++)
        {
            for (j = i; j >= gap; j -= gap)
            {
                p = b + (j - gap) * size;
                q = b + j * size;
                if (compar(p, q) <= 0)
                    break;
                for (k = 0; k < size; k++)
                {
                    t = p[k];
                    p[k] = q[k];
                    q[k] = t;
                }
            }
        }
    }
}

long strtol(const char *RESTRICT nptr, char **RESTRICT endptr, int base)
{
    const char *s = nptr;
    unsigned long v = 0, limit, cutoff;
    int neg = 0, any = 0, overflow = 0, d, cutlim;

    while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
        s++;
    if (*s == '-' || *s == '+')
        neg = (*s++ == '-');

    /* "0x" is a prefix only if a hex digit follows, else "0" is the number */
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')
        && ((s[2] >= '0' && s[2] <= '9') || ((s[2] | 0x20) >= 'a' && (s[2] | 0x20) <= 'f')))
    {
        s += 2;
        base = 16;
    }
    else if (base == 0)
        base = (*s == '0') ? 8 : 10;

    /* as the standard's: out of range values saturate (there is no errno) */
    limit = neg ? (unsigned long)__LONG_MAX__ + 1UL : (unsigned long)__LONG_MAX__;
    cutoff = limit / base;
    cutlim = (int)(limit % base);

    for (;; s++)
    {
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (*s >= 'a' && *s <= 'z')
            d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z')
            d = *s - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        any = 1;
        if (overflow || v > cutoff || (v == cutoff && d > cutlim))
            overflow = 1;           /* keep going, for endptr */
        else
            v = v * base + d;
    }

    if (endptr)
        *endptr = (char *)(any ? s : nptr);

    if (overflow)
        v = limit;
    if (!neg)
        return (long)v;

    return (v == limit) ? -__LONG_MAX__ - 1L : -(long)v;
}
