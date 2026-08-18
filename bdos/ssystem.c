/*
 * ssystem.c - GEMDOS Ssystem() -- the cookie jar and system variables,
 * without direct memory access or Supexec()
 *
 * Ssystem() is the documented (MiNT) mechanism for a program to read or
 * write a handful of system variables and the cookie jar without poking
 * memory directly. pTOS implements only the S_[GS]ET(COOKIE|[LWB]VAL)
 * subset -- see ssystem.h and issue #219 for the rationale, in
 * particular for ARM, where Supexec() (the usual way real TOS programs
 * would do this) isn't supported.
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
#include "../include/ahdi.h"

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
 * ssystem_getcookie/ssystem_setcookie - S_GETCOOKIE/S_SETCOOKIE
 *
 * only the plain by-tag lookup is supported (not the undocumented
 * slot-enumeration behaviour of the real ABI)
 */
static LONG ssystem_getcookie(LONG tag, LONG arg2)
{
    struct cookie *jar;

    for (jar = (struct cookie *)p_cookies; jar->tag; jar++)
    {
        if (jar->tag == tag)
        {
            if (arg2)
                *(LONG *)arg2 = jar->value;
            return jar->value;
        }
    }

    return -1;
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


/*
 * ssystem_getval/ssystem_setval - S_GET[LWB]VAL/S_SET[LWB]VAL
 */
static LONG ssystem_getlval(LONG addr)
{
    void *p = SVAL_LOOKUP(lval_table, addr);
    if (!p)
        return EINVFN;
    return *(LONG *)p;
}

static LONG ssystem_getwval(LONG addr)
{
    void *p = SVAL_LOOKUP(wval_table, addr);
    if (!p)
        return EINVFN;
    return *(WORD *)p;
}

static LONG ssystem_getbval(LONG addr)
{
    void *p = SVAL_LOOKUP(bval_table, addr);
    if (!p)
        return EINVFN;
    return *(BYTE *)p;
}

static LONG ssystem_setlval(LONG addr, LONG value)
{
    void *p = SVAL_LOOKUP(lval_table, addr);
    if (!p)
        return EINVFN;
    *(LONG *)p = value;
    return E_OK;
}

static LONG ssystem_setwval(LONG addr, LONG value)
{
    void *p = SVAL_LOOKUP(wval_table, addr);
    if (!p)
        return EINVFN;
    *(WORD *)p = (WORD)value;
    return E_OK;
}

static LONG ssystem_setbval(LONG addr, LONG value)
{
    void *p = SVAL_LOOKUP(bval_table, addr);
    if (!p)
        return EINVFN;
    *(BYTE *)p = (BYTE)value;
    return E_OK;
}


/*
 * xssystem - implements GEMDOS Ssystem(), see ssystem.h
 */
LONG xssystem(WORD mode, LONG arg1, LONG arg2)
{
    switch (mode)
    {
    case S_GETCOOKIE:
        return ssystem_getcookie(arg1, arg2);
    case S_SETCOOKIE:
        return ssystem_setcookie(arg1, arg2);

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

    default:
        return EINVFN;
    }
}
