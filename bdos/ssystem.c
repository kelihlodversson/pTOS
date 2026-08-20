/*
 * ssystem.c - GEMDOS Ssystem() -- the cookie jar and system variables,
 * without direct memory access or Supexec()
 *
 * Ssystem() is the documented (MiNT) mechanism for a program to read or
 * write a handful of system variables and the cookie jar without poking
 * memory directly. pTOS implements S_INQUIRE (the mandatory discovery
 * probe -- see xssystem() below), S_OSNAME/S_OSVERSION, and the
 * S_[GS]ET(COOKIE|[LWB]VAL) subset plus S_DELCOOKIE -- see ssystem.h
 * and issue #219 for the rationale, in particular for ARM, where
 * Supexec() (the usual way real TOS programs would do this) isn't
 * supported.
 *
 * S_CONSOLE_DIM is the one mode here that isn't part of that MiNT
 * protocol -- see its own comment in ssystem.h and issue #235.
 *
 * The *VAL modes never touch raw memory: the "address" argument is the
 * documented TOS address of the variable, looked up in one of three
 * width-specific tables that map it to wherever pTOS actually stores
 * that variable. An address that isn't in the table for the requested
 * width -- because the width is wrong, or the variable isn't one pTOS
 * implements -- returns EINVFN, same as an unknown mode.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "ssystem.h"
#include "gemerror.h"
#include "../bios/tosvars.h"
#include "../bios/cookie.h"
#include "../bios/lineavars.h"
#include "ahdi.h"
#include "string.h"

/* not in tosvars.h -- only bios/arch/arm/vectors.c otherwise uses it */
extern volatile LONG vbclock;

/*
 * one lookup table per width, each mapping the documented TOS address
 * of a system variable to pTOS's actual storage for it
 */
typedef struct {
    ULONG addr;
    void *ptr;
} SVAR;

#define SVAR_ENTRY(a, sym) { (a), (void *)&(sym) }

static const SVAR lval_table[] = {
    SVAR_ENTRY(0x400, etv_timer),
    SVAR_ENTRY(0x404, etv_critic),
    SVAR_ENTRY(0x408, etv_term),
    SVAR_ENTRY(0x42e, phystop),
    SVAR_ENTRY(0x432, membot),
    SVAR_ENTRY(0x436, memtop),
    SVAR_ENTRY(0x44e, v_bas_ad),
    SVAR_ENTRY(0x45a, colorptr),
    SVAR_ENTRY(0x456, vblqueue),
    SVAR_ENTRY(0x462, vbclock),
    SVAR_ENTRY(0x466, frclock),
    SVAR_ENTRY(0x46a, hdv_init),
    SVAR_ENTRY(0x472, hdv_bpb),
    SVAR_ENTRY(0x476, hdv_rw),
    SVAR_ENTRY(0x47a, hdv_boot),
    SVAR_ENTRY(0x47e, hdv_mediach),
    SVAR_ENTRY(0x4a2, savptr),
    SVAR_ENTRY(0x4ba, hz_200),
    SVAR_ENTRY(0x4c2, drvbits),
    SVAR_ENTRY(0x4c6, dskbufp),
    SVAR_ENTRY(0x4f2, sysbase),
    SVAR_ENTRY(0x4fa, end_os),
    SVAR_ENTRY(0x4fe, exec_os),
    SVAR_ENTRY(0x502, dump_vec),
    SVAR_ENTRY(0x506, prt_stat),
    SVAR_ENTRY(0x50a, prt_vec),
    SVAR_ENTRY(0x50e, aux_stat),
    SVAR_ENTRY(0x512, aux_vec),
    SVAR_ENTRY(0x516, pun_ptr),
    SVAR_ENTRY(0x5ac, bell_hook),
    SVAR_ENTRY(0x5b0, kcl_hook),
};

static const SVAR wval_table[] = {
    SVAR_ENTRY(0x43e, flock),
    SVAR_ENTRY(0x440, seekrate),
    SVAR_ENTRY(0x442, timer_ms),
    SVAR_ENTRY(0x444, fverify),
    SVAR_ENTRY(0x446, bootdev),
    SVAR_ENTRY(0x452, vblsem),
    SVAR_ENTRY(0x454, nvbls),
    SVAR_ENTRY(0x482, cmdload),
    SVAR_ENTRY(0x4a6, nflops),
    SVAR_ENTRY(0x4ac, save_row),
    SVAR_ENTRY(0x4ee, dumpflg),
};

static const SVAR bval_table[] = {
    SVAR_ENTRY(0x44a, defshiftmod),
    SVAR_ENTRY(0x44c, sshiftmod),
    SVAR_ENTRY(0x484, conterm),
};

#undef SVAR_ENTRY


static void *sval_lookup(const SVAR *table, int count, ULONG addr)
{
    int i;

    for (i = 0; i < count; i++)
        if (table[i].addr == addr)
            return table[i].ptr;

    return NULL;
}

#define SVAL_LOOKUP(table, addr) sval_lookup(table, ARRAY_SIZE(table), (addr))


/*
 * ssystem_getcookie/ssystem_setcookie/ssystem_delcookie -
 * S_GETCOOKIE/S_SETCOOKIE/S_DELCOOKIE
 *
 * S_GETCOOKIE overloads arg1 three ways -- required for code that
 * wants to enumerate every installed cookie, not just look one up by
 * a tag it already knows:
 *   - arg1 > 0xffff: a tag, look up its value (the common case)
 *   - 1 <= arg1 <= 0xffff: a 1-based slot number, look up the tag
 *     stored there -- ERR once past the jar's allocated capacity, so
 *     a caller can enumerate by counting 1, 2, 3, ... until either a
 *     zero tag (no more cookies) or ERR (ran off the end)
 *   - arg1 == 0: look up the NULL cookie's value, i.e. the jar's
 *     total allocated capacity (the terminator slot always carries
 *     this in .value, wherever it currently sits)
 */
static LONG ssystem_getcookie(LONG arg1, LONG arg2)
{
    struct cookie *jar = (struct cookie *)p_cookies;
    struct cookie *term;
    LONG capacity, result;

    if ((ULONG)arg1 > 0xffffUL)
    {
        for (; jar->tag; jar++)
        {
            if (jar->tag == arg1)
            {
                /*
                 * with a destination pointer, the documented usage is
                 * Ssystem(S_GETCOOKIE, tag, &value) == E_OK --
                 * returning the (possibly nonzero) cookie value here
                 * instead would look like failure to that idiom.
                 */
                if (arg2)
                {
                    *(LONG *)arg2 = jar->value;
                    return E_OK;
                }
                return jar->value;
            }
        }
        return ERR;
    }

    for (term = jar; term->tag; term++)
        /* find the terminator */;
    capacity = term->value;

    if (arg1 == 0)
    {
        result = capacity;
    }
    else
    {
        if (arg1 > capacity)
            return ERR;
        result = jar[arg1 - 1].tag;
    }

    if (arg2)
    {
        *(LONG *)arg2 = result;
        return E_OK;
    }
    return result;
}

static LONG ssystem_setcookie(LONG tag, LONG value)
{
    struct cookie *jar = (struct cookie *)p_cookies;
    LONG capacity;

    while (jar->tag)
    {
        if (jar->tag == tag)
        {
            jar->value = value;
            return E_OK;
        }
        jar++;
    }

    capacity = jar->value;      /* the terminator slot holds the jar's capacity */
    if ((jar - (struct cookie *)p_cookies) >= capacity - 1)
        return ENSMEM;          /* jar full */

    jar->tag = tag;
    jar->value = value;
    jar[1].tag = 0;
    jar[1].value = capacity;

    return E_OK;
}

static LONG ssystem_delcookie(LONG tag)
{
    struct cookie *jar = (struct cookie *)p_cookies;
    int i;

    for (i = 0; jar[i].tag; i++)
    {
        if (jar[i].tag == tag)
        {
            /* shift every later entry (including the terminator, which
             * carries the jar's capacity in .value) down by one slot */
            do
            {
                jar[i] = jar[i + 1];
                i++;
            } while (jar[i].tag);
            return E_OK;
        }
    }

    return ERR;         /* tag not found -- matches ssystem_getcookie() */
}


/*
 * svar_copy - like memcpy(), but through volatile UBYTE * so the
 * access can't be cached, reordered, or optimized away.
 *
 * Several of the looked-up sysvars are pointers or function pointers,
 * not LONG/WORD/BYTE objects, so this can't just dereference p through
 * an unrelated scalar type -- that violates C's strict-aliasing/
 * effective-type rules and can miscompile under optimization even
 * though it happens to work today. A byte-at-a-time UBYTE access is
 * always strict-aliasing-safe (any object's representation can be
 * accessed through a character type). But several of these sysvars
 * are also volatile (hz_200, frclock, vblsem, flock, vbclock, ...),
 * updated from interrupt context, and memcpy()'s plain void*
 * parameters silently drop that qualifier -- accessing through
 * volatile UBYTE * keeps every individual byte access an observable
 * side effect regardless of the pointed-to object's declared type.
 */
static void svar_copy(void *dst, const void *src, size_t n)
{
    volatile UBYTE *d = dst;
    const volatile UBYTE *s = src;

    while (n--)
        *d++ = *s++;
}

/*
 * ssystem_getval/ssystem_setval - S_GET[LWB]VAL/S_SET[LWB]VAL
 */
static LONG ssystem_getlval(LONG addr)
{
    void *p = SVAL_LOOKUP(lval_table, addr);
    LONG value;
    if (!p)
        return EINVFN;
    svar_copy(&value, p, sizeof(value));
    return value;
}

static LONG ssystem_getwval(LONG addr)
{
    void *p = SVAL_LOOKUP(wval_table, addr);
    UWORD value;    /* zero-extended into the LONG result, matching FreeMiNT */
    if (!p)
        return EINVFN;
    svar_copy(&value, p, sizeof(value));
    return value;
}

static LONG ssystem_getbval(LONG addr)
{
    void *p = SVAL_LOOKUP(bval_table, addr);
    UBYTE value;    /* zero-extended into the LONG result, matching FreeMiNT */
    if (!p)
        return EINVFN;
    svar_copy(&value, p, sizeof(value));
    return value;
}

static LONG ssystem_setlval(LONG addr, LONG value)
{
    void *p = SVAL_LOOKUP(lval_table, addr);
    if (!p)
        return EINVFN;
    svar_copy(p, &value, sizeof(value));
    return E_OK;
}

static LONG ssystem_setwval(LONG addr, LONG value)
{
    void *p = SVAL_LOOKUP(wval_table, addr);
    WORD wvalue = (WORD)value;
    if (!p)
        return EINVFN;
    svar_copy(p, &wvalue, sizeof(wvalue));
    return E_OK;
}

static LONG ssystem_setbval(LONG addr, LONG value)
{
    void *p = SVAL_LOOKUP(bval_table, addr);
    BYTE bvalue = (BYTE)value;
    if (!p)
        return EINVFN;
    svar_copy(p, &bvalue, sizeof(bvalue));
    return E_OK;
}


/*
 * ssystem_osname/ssystem_osversion - S_OSNAME/S_OSVERSION
 */
static LONG ssystem_osname(void)
{
    /* four ASCII characters packed MSB-first, same convention as
     * FreeMiNT's 'MiNT' (0x4d694e54) */
    return ((LONG)'p' << 24) | ((LONG)'T' << 16) | ((LONG)'O' << 8) | 'S';
}

static LONG ssystem_osversion(void)
{
    /*
     * major.minor.patch + release character, MSB first. pTOS has no
     * numbered release yet -- report 0.0.0-alpha rather than
     * repurposing GEMDOS_VERSION, which fakes a *real TOS's*
     * compatibility number (Sversion()) and has nothing to do with
     * pTOS's own identity.
     */
    LONG major = 0, minor = 0, patch = 0;
    BYTE release = 'a';
    return (major << 24) | (minor << 16) | (patch << 8) | (UBYTE)release;
}


/*
 * ssystem_console_dim - S_CONSOLE_DIM (pTOS extension, see ssystem.h)
 *
 * v_cel_mx/v_cel_my (bios/lineavars.h) hold the screen's last valid
 * column/row index, i.e. one less than the actual width/height -- add 1
 * to report the usable count a caller like getwh()/getht() actually
 * wants.
 *
 * arg1 is the caller's struct console_dim*, arg2 its sizeof() -- only
 * min(arg2, sizeof(our struct)) bytes are copied, so a caller's struct
 * that's smaller (built against an older pTOS) or larger (built against
 * a newer one than this kernel implements) than ours is both handled
 * correctly; see ssystem.h. arg2 == -1 queries sizeof(our struct)
 * without writing anything (arg1 is ignored).
 */
static LONG ssystem_console_dim(LONG arg1, LONG arg2)
{
    struct console_dim dim;
    LONG n;

    if (arg2 == -1)
        return (LONG)sizeof(dim);

    if (!arg1 || arg2 <= 0)
        return EINVFN;

    dim.width = linea_vars.v_cel_mx + 1;
    dim.height = linea_vars.v_cel_my + 1;

    n = arg2 < (LONG)sizeof(dim) ? arg2 : (LONG)sizeof(dim);
    svar_copy((void *)arg1, &dim, n);
    return n;
}


/*
 * xssystem - implements GEMDOS Ssystem(), see ssystem.h
 */
LONG xssystem(WORD mode, LONG arg1, LONG arg2)
{
    switch (mode)
    {
    case S_INQUIRE:
        /* the documented discovery probe: callers use this to decide
         * whether to use Ssystem() at all before falling back to
         * Supexec()/direct memory access -- which ARM doesn't have,
         * so this must not fall through to EINVFN like every other
         * unhandled mode. */
        return E_OK;

    case S_OSNAME:
        return ssystem_osname();
    case S_OSVERSION:
        return ssystem_osversion();

    case S_GETCOOKIE:
        return ssystem_getcookie(arg1, arg2);
    case S_SETCOOKIE:
        return ssystem_setcookie(arg1, arg2);
    case S_DELCOOKIE:
        return ssystem_delcookie(arg1);

    case S_GETLVAL:
        return ssystem_getlval(arg1);
    case S_GETWVAL:
        return ssystem_getwval(arg1);
    case S_GETBVAL:
        return ssystem_getbval(arg1);

    case S_SETLVAL:
        return ssystem_setlval(arg1, arg2);
    case S_SETWVAL:
        return ssystem_setwval(arg1, arg2);
    case S_SETBVAL:
        return ssystem_setbval(arg1, arg2);

    case S_CONSOLE_DIM:
        return ssystem_console_dim(arg1, arg2);

    default:
        return EINVFN;
    }
}
