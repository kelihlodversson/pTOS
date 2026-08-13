/*      GEMRSLIB.C      5/14/84 - 06/23/85      Lowell Webster          */
/*      merge High C vers. w. 2.2               8/24/87         mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2017 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Application Environment Services              Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1987                      Digital Research Inc.
*       -------------------------------------------------------------
*/

#include "config.h"
#include "portab.h"
#include "struct.h"
#include "basepage.h"
#include "obdefs.h"
#include "rsdefs.h"
#include "gemlib.h"
#include "gem_rsc.h"

#include "gemdos.h"
#include "gemshlib.h"
#include "gemgraf.h"
#include "geminit.h"
#include "gemrslib.h"
#include "gemgsxif.h"
#include "rsload.h"
#include "endian.h"

#include "string.h"
#include "nls.h"

/*
 * defines & typedefs
 */




/*******  LOCALS  **********************/

RSHDR   *rs_hdr;
AESGLOBAL *rs_global;
static char    tmprsfname[128];
static char    free_str[256];   /* must be long enough for longest freestring in gem.rsc */

/*
 *  Fix up a character position, from offset,row/col to a pixel value.
 *  If width is 80 then convert to full screen width.
 */
static void fix_chpos(WORD *pfix, WORD offset)
{
    WORD coffset;
    WORD cpos;

    cpos = *pfix;
    coffset = (cpos >> 8) & 0x00ff;
    cpos &= 0x00ff;

    switch(offset)
    {
    case 0:
        cpos *= gl_wchar;
        break;
    case 1:
        cpos *= gl_hchar;
        break;
    case 2:
        if (cpos == 80)
            cpos = gl_width;
        else
            cpos *= gl_wchar;
        break;
    case 3:
        cpos *= gl_hchar;
        break;
    }

    cpos += ( coffset > 128 ) ? (coffset - 256) : coffset;
    *pfix = cpos;
}


/************************************************************************/
/* rs_obfix                                                             */
/************************************************************************/
void rs_obfix(OBJECT *tree, WORD curob)
{
    WORD offset;
    WORD *p;

    /* set X,Y,W,H */
    p = &tree[curob].ob_x;

    for (offset=0; offset<4; offset++)
        fix_chpos(p+offset, offset);
}


void *get_sub(UWORD rsindex, UWORD offset, UWORD rsize)
{
    /* get base of objects and then index in */
    return (char *)rs_hdr + offset + rsize * rsindex;
}


/*
 *  return address of given type and index, INTERNAL ROUTINE
 */
void *get_addr(UWORD rstype, UWORD rsindex)
{
    WORD size;
    UWORD offset;
    OBJECT *obj;
    TEDINFO *tedinfo;
    ICONBLK *iconblk;
    RSHDR *hdr = rs_hdr;

    switch(rstype)
    {
    case R_TREE:
        return rs_global->ap_ptree[rsindex];
    case R_OBJECT:
        offset = hdr->rsh_object;
        size = sizeof(OBJECT);
        break;
    case R_TEDINFO:
    case R_TEPTEXT: /* same, because te_ptext is first field of TEDINFO */
        offset = hdr->rsh_tedinfo;
        size = sizeof(TEDINFO);
        break;
    case R_ICONBLK:
    case R_IBPMASK: /* same, because ib_pmask is first field of ICONBLK */
        offset = hdr->rsh_iconblk;
        size = sizeof(ICONBLK);
        break;
    case R_BITBLK:
    case R_BIPDATA: /* same, because bi_pdata is first field of BITBLK */
        offset = hdr->rsh_bitblk;
        size = sizeof(BITBLK);
        break;
    case R_OBSPEC:
        obj = (OBJECT *)get_addr(R_OBJECT, rsindex);
        return &obj->ob_spec;
    case R_TEPTMPLT:
    case R_TEPVALID:
        tedinfo = (TEDINFO *)get_addr(R_TEDINFO, rsindex);
        if (rstype == R_TEPTMPLT)
            return &tedinfo->te_ptmplt;
        return &tedinfo->te_pvalid;
    case R_IBPDATA:
    case R_IBPTEXT:
        iconblk = (ICONBLK *)get_addr(R_ICONBLK, rsindex);
        if (rstype == R_IBPDATA)
            return &iconblk->ib_pdata;
        return &iconblk->ib_ptext;
    case R_STRING:
        return *((void **)get_sub(rsindex, hdr->rsh_frstr, sizeof(LONG)));
    case R_IMAGEDATA:
        return *((void **)get_sub(rsindex, hdr->rsh_frimg, sizeof(LONG)));
    case R_FRSTR:
        offset = hdr->rsh_frstr;
        size = sizeof(LONG);
        break;
    case R_FRIMG:
        offset = hdr->rsh_frimg;
        size = sizeof(LONG);
        break;
    default:
        return (void *)-1L;
    }

    return get_sub(rsindex, offset, size);
} /* get_addr() */


#if CONF_WITH_COLOUR_ICONS
/*
 * returns pointer to a CICON that best matches the current resolution
 *
 * the order of preference is as follows:
 *  1. the CICON with the same number of planes as the current resolution
 *  2. of those CICONS with fewer planes than the current resolution, the
 *     one with the most number of planes
 * if neither applies, NULL is returned
 */
static CICON *best_match(CICONBLK *start)
{
    CICON *p, *found = NULL;

    for (p = start->mainlist; p; p = p->next_res)
    {
        if (p->num_planes > gl_nplanes)     /* too many planes */
            continue;
        if (p->num_planes == gl_nplanes)    /* exact match */
            return p;
        if (!found || (p->num_planes > found->num_planes))
            found = p;                      /* best so far */
    }

    return found;
}

/*
 * expand cicon data from S to D planes (S is strictly less than D)
 *
 * we use the same algorithm as Atari TOS:
 *  1. copy the source to the first S (0 to S-1) planes of the
 *     destination data
 *  2. create the Sth plane of the destination data by ANDing
 *     together all the source planes
 *  3. copy the Sth plane of the destination data to the remaining
 *     destination planes
 *  4. AND all the destination planes with the mask plane
 */
static void expand_cicondata(WORD *src, WORD *dst, WORD *mask, WORD w, WORD h, WORD src_planes, WORD dst_planes)
{
    WORD *p, *q;
    WORD plane_words, src_words;
    WORD i, j;

    plane_words = w / 16 * h;           /* in WORDS */
    src_words = plane_words * src_planes;

    /*
     * 1. the first src_planes of dst are the same as src
     */
    memcpy(dst, src, src_words*sizeof(WORD));

    /*
     * 2. copy the zeroth src plane to the next dst plane,
     *    then AND in the remaining planes
     */
    memcpy(dst+src_words, src, plane_words*sizeof(WORD));
    p = src + plane_words;      /* p -> start of remainder */
    for (i = 1; i < src_planes; i++)
    {
        q = dst + src_words;    /* q -> target */
        for (j = 0; j < plane_words; j++, p++, q++)
            *q = *p & *q;
    }

    /*
     * 3. copy the ANDed plane to the rest of the destination planes
     */
    p = dst + src_words;        /* p -> source (ANDed) plane */
    q = p + plane_words;        /* q -> start of remaining planes */
    for (i = 1; i < (dst_planes-src_planes); i++, q += plane_words)
    {
        memcpy(q, p, plane_words*sizeof(WORD));
    }

    /*
     * 4. AND the mask plane into all destination planes
     */
    q = dst;
    for (i = 0; i < dst_planes; i++)
    {
        p = mask;
        for (j = 0; j < plane_words; j++, p++, q++)
            *q &= *p;
    }
}

/*
 * transform a colour icon from device-independent to device-dependent form
 */
static void transform_cicon(WORD *src, WORD *dest, WORD w, WORD h, WORD planes)
{
    gsx_fix(&gl_src, src, w/8, h);
    gl_src.fd_stand = TRUE;
    gl_src.fd_nplanes = planes;

    gsx_fix(&gl_dst, dest, w/8, h);
    gl_dst.fd_nplanes = planes;

    vrn_trnfm(&gl_src, &gl_dst);
}

#if CONF_WITH_VDI_BACKEND_TRUECOLOR
/*
 *  pack one plane-major colour array (the RSC layout transform_cicon()
 *  reads) into w*h packed pixels: each pixel's colour code is the OR of
 *  its bit across the planes, then mapped to the active-format packed
 *  pixel via the physical workstation's palette.  RGB565 pixels occupy
 *  UWORD storage; XRGB8888 pixels occupy ULONG storage.
 *
 *  The bit order below -- plane p contributes bit (1<<p) of the colour
 *  code -- is the calibration constant the design calls out; the first
 *  screencap (Task 6) confirms or flips it to (1 << (planes-1-p)).
 */
static void pack_planes(const WORD *data, void *pixels, WORD planes, WORD w, WORD h, WORD pixel_size)
{
    LONG mono_words = ((LONG)w + 15) / 16;
    UWORD *pix16 = pixels;
    ULONG *pix32 = pixels;
    WORD x, y, p;

    for (y = 0; y < h; y++)
    {
        const WORD *rowbase = data + (LONG)y * mono_words;

        for (x = 0; x < w; x++)
        {
            UWORD mask = 0x8000 >> (x & 0x0f);
            WORD code = 0;

            for (p = 0; p < planes; p++)
            {
                const WORD *plane = rowbase + (LONG)p * mono_words * h;
                if (plane[x >> 4] & mask)
                    code |= (WORD)(1UL << p);
            }
            if (pixel_size == sizeof(UWORD))
                *pix16++ = (UWORD)vdi_truecolor_pixel_for_index(code);
            else
                *pix32++ = vdi_truecolor_pixel_for_index(code);
        }
    }
}

/*
 *  pack_cicon: convert the selected CICON's standard-format colour data
 *  to the packed-truecolor layout.  The normal and (optional) selected
 *  buffers use the active pixel format and are packed back-to-back in a
 *  single allocation so
 *  free_cicon_buffers() (which frees only cicon->col_data) still works.
 */
static BOOL pack_cicon(CICON *cicon, WORD w, WORD h)
{
    LONG pixels = (LONG)w * h;
    void *packed;
    WORD pixel_size;

    pixel_size = vdi_truecolor_pixel_size();
    packed = dos_alloc_anyram(pixels * (cicon->sel_data ? 2 : 1) * pixel_size);
    if (!packed)
        return FALSE;

    pack_planes(cicon->col_data, packed, cicon->num_planes, w, h, pixel_size);
    cicon->col_data = (WORD *)packed;

    if (cicon->sel_data)
    {
        if (pixel_size == sizeof(UWORD))
        {
            UWORD *selbuf = (UWORD *)packed + pixels;

            pack_planes(cicon->sel_data, selbuf, cicon->num_planes, w, h, pixel_size);
            cicon->sel_data = (WORD *)selbuf;
        }
        else
        {
            ULONG *selbuf = (ULONG *)packed + pixels;

            pack_planes(cicon->sel_data, selbuf, cicon->num_planes, w, h, pixel_size);
            cicon->sel_data = (WORD *)selbuf;
        }
    }

    cicon->num_planes = 1;
    return TRUE;
}
#endif

/*
 * initialise the colour icon stuff
 *
 * this includes:
 *  . filling in the CICONBLK pointer table
 *  . for each CICONBLK:
 *      . fixing up all of the internal data/mask/text pointers
 *      . determining the appropriate icon for the current resolution
 *      . expanding the icon if necessary
 *      . converting the icon to device-dependent form
 */
/*
 * for each CICONBLK in the resource, select the CICON with the number of
 * planes that best matches the current resolution.  then expand the icon
 * if necessary, and transform it from standard to device-dependent format
 */
void transform_all_cicons(LONG num_cicons, CICONBLK **ciconblkptr)
{
    CICONBLK *ciconblk;
    CICON *cicon;
    WORD *colbuf, *selbuf, *expandbuf, *src;
    LONG data_size, n;
    BOOL expand;
    WORD i, w, h;

    for (i = 0; i < num_cicons; i++)
    {
        ciconblk = ciconblkptr[i];
        cicon = best_match(ciconblk);   /* find a suitable CICON */
        ciconblk->mainlist = cicon;
        if (!cicon)                     /* nothing suitable ... */
            continue;
        w = ciconblk->monoblk.ib_wicon;
        h = ciconblk->monoblk.ib_hicon;
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
        /*
         * Packed-truecolor screen: there are no bitplanes to expand to.
         * Convert the standard-format colour planes straight to one
         * active-format pixel per bit (w*h UWORDs for RGB565, ULONGs for
         * XRGB8888, num_planes=1).  Skipping
         * expand_cicondata()/transform_cicon() here is what avoids the
         * 16-plane interleaved form that setup_info() cannot interpret.
         * Pixels whose colour code is 0 keep their own palette colour
         * (the icon background -- the mask blit in gr_gicon() is what
         * paints the object's background over the shape); this is the
         * deliberate truecolor look, see the design doc.
         */
        if (vdi_truecolor_screen())
        {
            cicon->next_res = NULL;
            if (!pack_cicon(cicon, w, h))
                ciconblk->mainlist = NULL;  /* no colour for this icon */
            continue;
        }
#endif
        data_size = (LONG)(w/8*gl_nplanes) * h;
        expand = (cicon->num_planes != gl_nplanes); /* boolean */

        /* if we need to expand the icon, we need a temp buffer */
        expandbuf = NULL;
        if (expand)
        {
            expandbuf = dos_alloc_anyram(data_size);
            if (!expandbuf)
            {
                ciconblk->mainlist = NULL;  /* no colour for this icon */
                continue;
            }
        }

        /* we always allocate a data buffer so we avoid transform-in-place */
        n = cicon->sel_data ? 2*data_size : data_size;
        colbuf = dos_alloc_anyram(n);
        if (!colbuf)
        {
            if (expandbuf)
                dos_free(expandbuf);
            ciconblk->mainlist = NULL;      /* no colour for this icon */
            continue;
        }

        /* handle standard icon */
        src = cicon->col_data;
        if (expand)
        {
            expand_cicondata(src, expandbuf, cicon->col_mask, w, h, cicon->num_planes, gl_nplanes);
            src = expandbuf;
        }
        transform_cicon(src, colbuf, w, h, gl_nplanes);
        cicon->col_data = colbuf;

        /* handle 'selected' icon (if present) */
        if (cicon->sel_data)
        {
            selbuf = colbuf + data_size/sizeof(WORD);
            src = cicon->sel_data;
            if (expand)
            {
                expand_cicondata(src, expandbuf, cicon->sel_mask, w, h, cicon->num_planes, gl_nplanes);
                src = expandbuf;
            }
            transform_cicon(src, selbuf, w, h, gl_nplanes);
            cicon->sel_data = selbuf;
        }

        cicon->num_planes = gl_nplanes;     /* neatness only */
        cicon->next_res = NULL;

        if (expandbuf)
            dos_free(expandbuf);
    }
}

/*
 * return pointer to start of CICONBLK pointer table
 *
 * returns NULL if none
 */
CICONBLK **get_ciconblkptr(RSHDR *hdr)
{
    LONG *extarray;
    LONG cptr_offset;

    /* check if we could have CICONs */
    if ((hdr->rsh_vrsn & NEW_FORMAT_RSC) == 0)
        return NULL;

    /*
     * locate extension array, which has the following format (all longs):
     *  extarray[0]     true length of RSC file
     *  extarray[1]     offset of CICON table (-1L => none present)
     *  extarray[2]...  other extensions
     *  extarray[n]     0L indicates end of array
     */
    extarray = (LONG *)((char *)hdr + hdr->rsh_rssize);

    /* do we have CICONs? */
    cptr_offset = extarray[1];
    if ((cptr_offset == 0L) || (cptr_offset == -1L))
        return NULL;

    return (CICONBLK **)((char *)hdr + cptr_offset);
}

/*
 * free the CICON-related buffers allocated by transform_all_cicons()
 *
 * returns -1 iff dos_free() failed
 */
static WORD free_cicon_buffers(RSHDR *hdr)
{
    CICONBLK **ciconblkptr, **p;
    CICON *cicon;
    WORD rc = 0;

    /* find the CICONBLK ptr table & count the CICONBLKs */
    ciconblkptr = get_ciconblkptr(hdr);
    if (!ciconblkptr)   /* yes, we have no CICONBLKs */
        return 0;

    /* free any buffers allocated by transform_all_cicons() */
    for (p = ciconblkptr; *p != (CICONBLK *)-1L; p++)
    {
        cicon = (*p)->mainlist;
        if (cicon)
            if (dos_free(cicon->col_data))
                rc = -1;
    }

    return rc;
}
#endif


/*
 *  Set global addresses that are used by the resource library subroutines
 */
static void rs_sglobe(AESGLOBAL *pglobal)
{
    rs_global = pglobal;
    rs_hdr = rs_global->ap_rscmem;
}


/*
 *  Free the memory associated with a particular resource load
 */
WORD rs_free(AESGLOBAL *pglobal)
{
    WORD rc = 1;    /* default rc => OK */

    rs_sglobe(pglobal);

#if CONF_WITH_COLOUR_ICONS
    if (free_cicon_buffers(rs_global->ap_rscmem))
        rc = 0;
#endif
    if (dos_free(rs_global->ap_rscmem))
        rc = 0;

    return rc;
}


/*
 *  Get a particular ADDRess out of a resource file that has been
 *  loaded into memory
 */
WORD rs_gaddr(AESGLOBAL *pglobal, UWORD rtype, UWORD rindex, void **rsaddr)
{
    rs_sglobe(pglobal);

    *rsaddr = get_addr(rtype, rindex);
    return (*rsaddr != (void *)-1L);
}


/*
 *  Set a particular ADDRess in a resource file that has been
 *  loaded into memory
 */
WORD rs_saddr(AESGLOBAL *pglobal, UWORD rtype, UWORD rindex, void *rsaddr)
{
    void **psubstruct;

    rs_sglobe(pglobal);

    psubstruct = (void **)get_addr(rtype, rindex);
    if (psubstruct != (void **)-1L)
    {
        *psubstruct = rsaddr;
        return TRUE;
    }

    return FALSE;
}


/*
 *  Fix up objects separately so that we can read GEM resource before we
 *  do an open workstation, then once we know the character sizes we
 *  can fix up the objects accordingly.
 */
void rs_fixit(AESGLOBAL *pglobal)
{
    rs_sglobe(pglobal);
    fix_objects();
}


/*
 *  rs_load: the rsrc_load() implementation
 */
WORD rs_load(AESGLOBAL *pglobal, BYTE *rsfname)
{
    LONG  dosrc;
    WORD  ret;
    UWORD fd;

    /*
     * use shel_find() to get resource location
     */
    strcpy(tmprsfname,rsfname);
    if (!sh_find(tmprsfname))
        return FALSE;

    dosrc = dos_open((BYTE *)tmprsfname,0); /* mode 0: read only */
    if (dosrc < 0L)
        return FALSE;
    fd = (UWORD)dosrc;

    ret = rs_readit(pglobal,fd);
    if (ret)
        rs_fixit(pglobal);
    dos_close(fd);

    return ret;
}


/* Get a string from the GEM-RSC */
BYTE *rs_str(UWORD stnum)
{
    strcpy(free_str, gettext(rs_fstr[stnum]));
    return free_str;
}
