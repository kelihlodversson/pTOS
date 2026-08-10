/*
 *  rsload_legacy.c - m68k in-place RSC loader
 *
 *  The former TOS resource loader: read the file into memory and fix
 *  resource offsets in place, relying on the m68k native structure layout
 *  matching Atari's big-endian resource-file layout.  Selected by build.mk
 *  when CONF_WITH_LEGACY_RSC_LOAD is set (the 192/256 KB ROMs).
 *
 *  Copyright (C) 2004-2017 The EmuTOS development team
 *  This file is distributed under the GPL, version 2 or at your
 *  option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "obdefs.h"
#include "rsdefs.h"
#include "gemdos.h"
#include "gemrslib.h"
#include "string.h"

#include "rsload.h"

#if CONF_WITH_COLOUR_ICONS
/*
 * fixup all of the CICONs for a CICONBLK
 *
 * returns a pointer to the next CICONBLK
 */
static CICONBLK *fixup_colour_icons(LONG num_cicons, LONG mono_words, CICON *start)
{
    CICON *p = start;
    WORD *q;
    LONG i;

    /*
     * loop through all the CICONs for one CICONBLK
     *
     * p points to the current CICON; q tracks sections of CICON data.
     * at the end, p will point to the start of the next CICONBLK
     */
    for (i = 0; i < num_cicons; i++)
    {
        q = (WORD *)(p+1);                  /* point to start of data area */
        p->col_data = q;
        q += mono_words * p->num_planes;
        p->col_mask = q;
        q += mono_words;                    /* mask is one plane */
        if (p->sel_data)
        {
            p->sel_data = q;
            q += mono_words * p->num_planes;
            p->sel_mask = q;
            q += mono_words;                /* mask is one plane */
        }
        if (p->next_res == (CICON *)1L)     /* more CICONs follow */
            p->next_res = (CICON *)q;
        else
            p->next_res = NULL;
        p = (CICON *)q;
    }

    return (CICONBLK *)p;
}

/*
 * this fixes up all of the CICONBLK-related pointers:
 *  . the CICONBLK pointer table
 *  . the pointers in the ICONBLK contained in the CICONBLK
 *  . the pointers in the CICON
 */
static void fixup_all_ciconblks(LONG num_blks, CICONBLK **ciconblkptr, CICON *cicondata)
{
    CICONBLK *p = (CICONBLK *)cicondata;
    WORD *q;
    LONG i, num_cicons, mono_words;

    for (i = 0; i < num_blks; i++)
    {
        ciconblkptr[i] = p;
        num_cicons = (LONG)(p->mainlist);   /* number of colour icons for this CICONBLK */
        mono_words = (((LONG)p->monoblk.ib_wicon + 15L) / 16L) * p->monoblk.ib_hicon;
        q = (WORD *)(p+1);                  /* point to start of data area */

        p->monoblk.ib_pdata = q;            /* fixup mono icon */
        q += mono_words;
        p->monoblk.ib_pmask = q;
        q += mono_words;
        p->monoblk.ib_ptext = (BYTE *)q;
        q += 12 / 2;                        /* length of icon text */
        if (num_cicons)
        {
            p->mainlist = (CICON *)q;
            p = fixup_colour_icons(num_cicons, mono_words, (CICON *)q);
        }
        else
        {
            p->mainlist = NULL;
            p = (CICONBLK *)q;
        }
    }
}
#endif

#if CONF_WITH_COLOUR_ICONS
static void fix_cicons(void)
{
    CICONBLK **ciconblkptr, **p;
    CICON *cicondata;
    LONG num_ciconblks;

    ciconblkptr = get_ciconblkptr(rs_hdr);
    if (!ciconblkptr)
        return;
    for (num_ciconblks = 0, p = ciconblkptr; *p != (CICONBLK *)-1L; p++)
        num_ciconblks++;
    cicondata = (CICON *)(p+1);
    fixup_all_ciconblks(num_ciconblks, ciconblkptr, cicondata);
    transform_all_cicons(num_ciconblks, ciconblkptr);
}
#endif

static BOOL fix_long(LONG *plong)
{
    LONG lngval;

    lngval = *plong;
    if (lngval != -1L)
    {
        *plong = (LONG)rs_hdr + lngval;
        return TRUE;
    }
    return FALSE;
}

static void fix_trindex(void)
{
    WORD ii;
    LONG *ptreebase;

    ptreebase = (LONG *)get_sub(R_TREE, rs_hdr->rsh_trindex, sizeof(LONG));
    rs_global->ap_ptree = (OBJECT **)ptreebase;
    for (ii = 0; ii < rs_hdr->rsh_ntree; ii++)
        fix_long(ptreebase+ii);
}

static void fix_nptrs(WORD cnt, WORD type)
{
    WORD i;

    for (i = 0; i < cnt; i++)
        fix_long(get_addr(type, i));
}

static BOOL fix_ptr(WORD type, WORD index)
{
    return fix_long(get_addr(type, index));
}

static void fix_tedinfo(void)
{
    WORD ii;
    TEDINFO *ted;

    for (ii = 0; ii < rs_hdr->rsh_nted; ii++)
    {
        ted = (TEDINFO *)get_addr(R_TEDINFO, ii);
        if (fix_ptr(R_TEPTEXT, ii))
            ted->te_txtlen = strlen(ted->te_ptext) + 1;
        if (fix_ptr(R_TEPTMPLT, ii))
            ted->te_tmplen = strlen(ted->te_ptmplt) + 1;
        fix_ptr(R_TEPVALID, ii);
    }
}

void fix_objects(void)
{
    WORD ii;
    WORD obtype;
    OBJECT *obj;
#if CONF_WITH_COLOUR_ICONS
    CICONBLK **ciconblkptr = get_ciconblkptr(rs_hdr);
    OBSPEC *spec;
#endif

    for (ii = 0; ii < rs_hdr->rsh_nobs; ii++)
    {
        obj = (OBJECT *)get_addr(R_OBJECT, ii);
        rs_obfix(obj, 0);
        obtype = obj->ob_type & 0x00ff;
        switch (obtype)
        {
        case G_CICON:
#if CONF_WITH_COLOUR_ICONS
            if (ciconblkptr)
            {
                if (obj->ob_flags & INDIRECT)
                    fix_long(&obj->ob_spec.index);
                spec = (obj->ob_flags & INDIRECT) ? obj->ob_spec.indirect : &obj->ob_spec;
                spec->ciconblk = ciconblkptr[spec->index];
            }
#endif
            break;
        case G_BOX:
        case G_IBOX:
        case G_BOXCHAR:
            break;
        default:
            fix_long(&obj->ob_spec.index);
            break;
        }
    }
}

WORD rs_readit(AESGLOBAL *pglobal, UWORD fd)
{
    WORD ibcnt;
    LONG rslsize;
    RSHDR hdr_buff;

    if (dos_read(fd, sizeof(hdr_buff), &hdr_buff) != sizeof(hdr_buff))
        return FALSE;
    rslsize = hdr_buff.rsh_rssize;
#if CONF_WITH_COLOUR_ICONS
    if (hdr_buff.rsh_vrsn & NEW_FORMAT_RSC)
    {
        if (dos_lseek(fd, 0, rslsize) < 0L)
            return FALSE;
        if (dos_read(fd, sizeof(rslsize), &rslsize) != sizeof(rslsize))
            return FALSE;
    }
#endif
    rs_hdr = (RSHDR *)dos_alloc_anyram(rslsize);
    if (!rs_hdr)
        return FALSE;
    if (dos_lseek(fd, 0, 0x0L) < 0L)
        goto fail;
    if (dos_read(fd, rslsize, rs_hdr) != rslsize)
        goto fail;
    rs_global = pglobal;
    rs_global->ap_rscmem = rs_hdr;
    rs_global->ap_rsclen = rslsize;
    fix_trindex();
#if CONF_WITH_COLOUR_ICONS
    fix_cicons();
#endif
    fix_tedinfo();
    ibcnt = rs_hdr->rsh_nib;
    fix_nptrs(ibcnt, R_IBPMASK);
    fix_nptrs(ibcnt, R_IBPDATA);
    fix_nptrs(ibcnt, R_IBPTEXT);
    fix_nptrs(rs_hdr->rsh_nbb, R_BIPDATA);
    fix_nptrs(rs_hdr->rsh_nstring, R_FRSTR);
    fix_nptrs(rs_hdr->rsh_nimages, R_FRIMG);
    return TRUE;
fail:
    dos_free(rs_hdr);
    rs_hdr = NULL;
    return FALSE;
}
