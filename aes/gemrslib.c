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
#include "endian.h"

#include "string.h"
#include "nls.h"

/*
 * defines & typedefs
 */

/* type definitions for use by an application when calling      */
/*  rsrc_gaddr and rsrc_saddr                                   */

#define R_TREE      0
#define R_OBJECT    1
#define R_TEDINFO   2
#define R_ICONBLK   3
#define R_BITBLK    4
#define R_STRING    5               /* gets pointer to free strings */
#define R_IMAGEDATA 6               /* gets pointer to free images  */
#define R_OBSPEC    7
#define R_TEPTEXT   8               /* sub ptrs in TEDINFO  */
#define R_TEPTMPLT  9
#define R_TEPVALID  10
#define R_IBPMASK   11              /* sub ptrs in ICONBLK  */
#define R_IBPDATA   12
#define R_IBPTEXT   13
#define R_BIPDATA   14              /* sub ptrs in BITBLK   */
#define R_FRSTR     15              /* gets addr of ptr to free strings     */
#define R_FRIMG     16              /* gets addr of ptr to free images      */



/*******  LOCALS  **********************/

static RSHDR   *rs_hdr;
static AESGLOBAL *rs_global;
static char    tmprsfname[128];
static char    free_str[256];   /* must be long enough for longest freestring in gem.rsc */

/* RSC records on disk are always big-endian and have these fixed sizes. */
#define DISK_RSHDR_SIZE     36
#define DISK_OBJECT_SIZE    24
#define DISK_TEDINFO_SIZE   28
#define DISK_ICONBLK_SIZE   34
#define DISK_BITBLK_SIZE    14

#define D_RSH_VRSN          0
#define D_RSH_OBJECT        2
#define D_RSH_TEDINFO       4
#define D_RSH_ICONBLK       6
#define D_RSH_BITBLK        8
#define D_RSH_FRSTR         10
#define D_RSH_STRING        12
#define D_RSH_IMDATA        14
#define D_RSH_FRIMG         16
#define D_RSH_TRINDEX       18
#define D_RSH_NOBS          20
#define D_RSH_NTREE         22
#define D_RSH_NTED          24
#define D_RSH_NIB           26
#define D_RSH_NBB           28
#define D_RSH_NSTRING       30
#define D_RSH_NIMAGES       32
#define D_RSH_RSSIZE        34

#define D_OBJ_NEXT          0
#define D_OBJ_HEAD          2
#define D_OBJ_TAIL          4
#define D_OBJ_TYPE          6
#define D_OBJ_FLAGS         8
#define D_OBJ_STATE         10
#define D_OBJ_SPEC          12
#define D_OBJ_X             16
#define D_OBJ_Y             18
#define D_OBJ_WIDTH         20
#define D_OBJ_HEIGHT        22

struct disk_rsc {
    const UBYTE *base;
    LONG size;
    RSHDR hdr;
};

struct native_rsc_layout {
    LONG object, tedinfo, iconblk, bitblk;
    LONG trindex, frstr, frimg, raw, total;
};

static BOOL disk_range(const struct disk_rsc *disk, LONG offset, LONG length)
{
    return (offset >= 0L) && (length >= 0L)
        && (offset <= disk->size) && (length <= disk->size-offset);
}

static UWORD disk_uword(const struct disk_rsc *disk, LONG offset)
{
    UWORD value;

    memcpy(&value, disk->base + offset, sizeof(value));
    return be2cpu16(value);
}

static ULONG disk_ulong(const struct disk_rsc *disk, LONG offset)
{
    ULONG value;

    memcpy(&value, disk->base + offset, sizeof(value));
    return be2cpu32(value);
}

static WORD disk_word(const struct disk_rsc *disk, LONG offset)
{
    return (WORD)disk_uword(disk, offset);
}

static void disk_decode_rshdr(struct disk_rsc *disk)
{
    RSHDR *hdr = &disk->hdr;

    hdr->rsh_vrsn = disk_uword(disk, D_RSH_VRSN);
    hdr->rsh_object = disk_uword(disk, D_RSH_OBJECT);
    hdr->rsh_tedinfo = disk_uword(disk, D_RSH_TEDINFO);
    hdr->rsh_iconblk = disk_uword(disk, D_RSH_ICONBLK);
    hdr->rsh_bitblk = disk_uword(disk, D_RSH_BITBLK);
    hdr->rsh_frstr = disk_uword(disk, D_RSH_FRSTR);
    hdr->rsh_string = disk_uword(disk, D_RSH_STRING);
    hdr->rsh_imdata = disk_uword(disk, D_RSH_IMDATA);
    hdr->rsh_frimg = disk_uword(disk, D_RSH_FRIMG);
    hdr->rsh_trindex = disk_uword(disk, D_RSH_TRINDEX);
    hdr->rsh_nobs = disk_word(disk, D_RSH_NOBS);
    hdr->rsh_ntree = disk_word(disk, D_RSH_NTREE);
    hdr->rsh_nted = disk_word(disk, D_RSH_NTED);
    hdr->rsh_nib = disk_word(disk, D_RSH_NIB);
    hdr->rsh_nbb = disk_word(disk, D_RSH_NBB);
    hdr->rsh_nstring = disk_word(disk, D_RSH_NSTRING);
    hdr->rsh_nimages = disk_word(disk, D_RSH_NIMAGES);
    hdr->rsh_rssize = disk_uword(disk, D_RSH_RSSIZE);
}

static BOOL disk_header(struct disk_rsc *disk, const void *base, LONG available)
{
    LONG length;

    if (available < DISK_RSHDR_SIZE)
        return FALSE;
    disk->base = base;
    disk->size = available;
    disk_decode_rshdr(disk);
    length = disk->hdr.rsh_rssize;
    if (disk->hdr.rsh_vrsn & NEW_FORMAT_RSC)
    {
        if (!disk_range(disk, length, sizeof(ULONG)))
            return FALSE;
        length = (LONG)disk_ulong(disk, length);
    }
    if ((length < DISK_RSHDR_SIZE) || (length > available))
        return FALSE;
    disk->size = length;
    return TRUE;
}

static LONG align_long(LONG value)
{
    return (value + 3L) & ~3L;
}

static BOOL layout_add(LONG *offset, LONG length)
{
    if ((length < 0L) || (*offset > 0x7fffffffL-length))
        return FALSE;
    *offset = align_long(*offset);
    if (*offset > 0x7fffffffL-length)
        return FALSE;
    *offset += length;
    return TRUE;
}

static BOOL layout_ordinary(const struct disk_rsc *disk, struct native_rsc_layout *layout)
{
    const RSHDR *hdr = &disk->hdr;
    LONG offset;

    if ((hdr->rsh_nobs < 0) || (hdr->rsh_ntree < 0) || (hdr->rsh_nted < 0)
        || (hdr->rsh_nib < 0) || (hdr->rsh_nbb < 0) || (hdr->rsh_nstring < 0)
        || (hdr->rsh_nimages < 0))
        return FALSE;
    offset = sizeof(RSHDR);
    layout->object = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nobs * sizeof(OBJECT))) return FALSE;
    layout->tedinfo = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nted * sizeof(TEDINFO))) return FALSE;
    layout->iconblk = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nib * sizeof(ICONBLK))) return FALSE;
    layout->bitblk = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nbb * sizeof(BITBLK))) return FALSE;
    layout->trindex = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_ntree * sizeof(void *))) return FALSE;
    layout->frstr = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nstring * sizeof(void *))) return FALSE;
    layout->frimg = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nimages * sizeof(void *))) return FALSE;
    layout->raw = align_long(offset);
    if (!layout_add(&offset, disk->size)) return FALSE;
    layout->total = offset;
    return layout->total <= 0xffffL;
}


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


static void *get_sub(UWORD rsindex, UWORD offset, UWORD rsize)
{
    /* get base of objects and then index in */
    return (char *)rs_hdr + offset + rsize * rsindex;
}


/*
 *  return address of given type and index, INTERNAL ROUTINE
 */
static void *get_addr(UWORD rstype, UWORD rsindex)
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
        mono_words = (LONG)(p->monoblk.ib_wicon/16) * p->monoblk.ib_hicon;
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
 *  its bit across the planes, then mapped to RGB565 via the physical
 *  workstation's palette.
 *
 *  The bit order below -- plane p contributes bit (1<<p) of the colour
 *  code -- is the calibration constant the design calls out; the first
 *  screencap (Task 6) confirms or flips it to (1 << (planes-1-p)).
 */
static void pack_planes(const WORD *data, UWORD *pix, WORD planes, WORD w, WORD h)
{
    WORD mono_words = w / 16;
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
                    code |= (WORD)(1 << p);
            }
            *pix++ = vdi_truecolor_pixel_for_index(code);
        }
    }
}

/*
 *  pack_cicon: convert the selected CICON's standard-format colour data
 *  to the packed-truecolor layout.  The normal and (optional) selected
 *  buffers are packed back-to-back in a single allocation so
 *  free_cicon_buffers() (which frees only cicon->col_data) still works.
 */
static BOOL pack_cicon(CICON *cicon, WORD w, WORD h)
{
    LONG pixels = (LONG)w * h;
    UWORD *packed;

    packed = dos_alloc_anyram(pixels * (cicon->sel_data ? 2 : 1) * sizeof(UWORD));
    if (!packed)
        return FALSE;

    pack_planes(cicon->col_data, packed, cicon->num_planes, w, h);
    cicon->col_data = (WORD *)packed;

    if (cicon->sel_data)
    {
        UWORD *selbuf = packed + pixels;

        pack_planes(cicon->sel_data, selbuf, cicon->num_planes, w, h);
        cicon->sel_data = (WORD *)selbuf;
    }

    cicon->num_planes = 1;
    return TRUE;
}
#endif

/*
 * for each CICONBLK in the resource, select the CICON with the number of
 * planes that best matches the current resolution.  then expand the icon
 * if necessary, and transform it from standard to device-dependent format
 */
static void transform_all_cicons(LONG num_cicons, CICONBLK **ciconblkptr)
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
         * RGB565 pixel per bit (w*h UWORDs, num_planes=1), the layout
         * truecolor_raster_copy()'s opaque path reads.  Skipping
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
static CICONBLK **get_ciconblkptr(RSHDR *hdr)
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
static void __attribute__((unused)) fix_cicons(void)
{
    RSHDR *hdr = rs_hdr;
    CICONBLK **ciconblkptr, **p;
    CICON *cicondata;
    LONG num_ciconblks;

    /* find the CICONBLK ptr table & count the CICONBLKs */
    ciconblkptr = get_ciconblkptr(hdr);
    if (!ciconblkptr)   /* yes, we have no CICONBLKs */
        return;

    for (num_ciconblks = 0, p = ciconblkptr; *p != (CICONBLK *)-1L; p++)
        num_ciconblks++;

    /* the CICON data area starts immediately after the pointer table */
    cicondata = (CICON *)(p+1);

    /* fixup the pointers in the resource */
    fixup_all_ciconblks(num_ciconblks, ciconblkptr, cicondata);

    /* transform all the icons to device-dependent format */
    transform_all_cicons(num_ciconblks, ciconblkptr);
}
#endif


static void fix_objects(void)
{
    WORD ii;
    WORD obtype;
    OBJECT *obj;
#if CONF_WITH_COLOUR_ICONS
    CICONBLK **ciconblkptr = get_ciconblkptr(rs_hdr);
#endif

    for (ii = 0; ii < rs_hdr->rsh_nobs; ii++)
    {
        obj = (OBJECT *)get_addr(R_OBJECT, ii);
        rs_obfix(obj, 0);
        obtype = obj->ob_type & 0x00ff;
        switch (obtype)
        {
#if CONF_WITH_COLOUR_ICONS
        case G_CICON:
            if (ciconblkptr)
                obj->ob_spec.index = (LONG)ciconblkptr[obj->ob_spec.index];
            break;
#endif
        case G_BOX:
        case G_IBOX:
        case G_BOXCHAR:
            break;
        default:
            break;
        }
    }
}

static void *native_disk_ptr(UBYTE *image, const struct native_rsc_layout *layout,
                             const struct disk_rsc *disk, LONG offset)
{
    if (!disk_range(disk, offset, 1L))
        return NULL;
    return image + layout->raw + offset;
}

static TEDINFO *native_tedinfo_ptr(UBYTE *image, const struct native_rsc_layout *layout,
                                   const struct disk_rsc *disk, ULONG offset)
{
    LONG index;

    if ((offset < disk->hdr.rsh_tedinfo)
        || (offset >= (ULONG)(disk->hdr.rsh_tedinfo + (LONG)disk->hdr.rsh_nted * DISK_TEDINFO_SIZE)))
        return NULL;
    index = (LONG)offset - disk->hdr.rsh_tedinfo;
    if (index % DISK_TEDINFO_SIZE)
        return NULL;
    return (TEDINFO *)(image + layout->tedinfo
        + (index/DISK_TEDINFO_SIZE) * sizeof(TEDINFO));
}

static ICONBLK *native_iconblk_ptr(UBYTE *image, const struct native_rsc_layout *layout,
                                   const struct disk_rsc *disk, ULONG offset)
{
    LONG index;

    if ((offset < disk->hdr.rsh_iconblk)
        || (offset >= (ULONG)(disk->hdr.rsh_iconblk + (LONG)disk->hdr.rsh_nib * DISK_ICONBLK_SIZE)))
        return NULL;
    index = (LONG)offset - disk->hdr.rsh_iconblk;
    if (index % DISK_ICONBLK_SIZE)
        return NULL;
    return (ICONBLK *)(image + layout->iconblk
        + (index/DISK_ICONBLK_SIZE) * sizeof(ICONBLK));
}

static BITBLK *native_bitblk_ptr(UBYTE *image, const struct native_rsc_layout *layout,
                                 const struct disk_rsc *disk, ULONG offset)
{
    LONG index;

    if ((offset < disk->hdr.rsh_bitblk)
        || (offset >= (ULONG)(disk->hdr.rsh_bitblk + (LONG)disk->hdr.rsh_nbb * DISK_BITBLK_SIZE)))
        return NULL;
    index = (LONG)offset - disk->hdr.rsh_bitblk;
    if (index % DISK_BITBLK_SIZE)
        return NULL;
    return (BITBLK *)(image + layout->bitblk
        + (index/DISK_BITBLK_SIZE) * sizeof(BITBLK));
}

static BOOL disk_string(const struct disk_rsc *disk, ULONG offset)
{
    LONG i;

    if (offset == (ULONG)-1L)
        return TRUE;
    if (offset >= (ULONG)disk->size)
        return FALSE;
    for (i = offset; i < disk->size; i++)
        if (!disk->base[i])
            return TRUE;
    return FALSE;
}

static BOOL copy_disk_words(WORD *dest, const struct disk_rsc *disk, LONG offset, LONG count)
{
    LONG i;

    if ((count < 0L) || !disk_range(disk, offset, count * 2L))
        return FALSE;
    for (i = 0; i < count; i++)
        dest[i] = disk_word(disk, offset + i*2L);
    return TRUE;
}

static BOOL materialize_rsc(AESGLOBAL *pglobal, const struct disk_rsc *disk)
{
    struct native_rsc_layout layout;
    RSHDR *hdr;
    UBYTE *image;
    OBJECT *obj;
    TEDINFO *ted;
    ICONBLK *icon;
    BITBLK *bit;
    OBJECT **trees;
    BYTE **frstr;
    void **frimg;
    ULONG spec;
    LONG i, off, words;

    if (!layout_ordinary(disk, &layout))
        return FALSE;
    image = dos_alloc_anyram(layout.total);
    if (!image)
        return FALSE;
    memset(image, 0, layout.total);
    hdr = (RSHDR *)image;
    *hdr = disk->hdr;
    if (!disk_range(disk, disk->hdr.rsh_string, 0L)
        || !disk_range(disk, disk->hdr.rsh_imdata, 0L)
        || (layout.object > 0xffffL) || (layout.tedinfo > 0xffffL)
        || (layout.iconblk > 0xffffL) || (layout.bitblk > 0xffffL)
        || (layout.trindex > 0xffffL) || (layout.frstr > 0xffffL)
        || (layout.frimg > 0xffffL) || (layout.raw + disk->hdr.rsh_string > 0xffffL)
        || (layout.raw + disk->hdr.rsh_imdata > 0xffffL))
        goto fail;
    hdr->rsh_object = layout.object;
    hdr->rsh_tedinfo = layout.tedinfo;
    hdr->rsh_iconblk = layout.iconblk;
    hdr->rsh_bitblk = layout.bitblk;
    hdr->rsh_trindex = layout.trindex;
    hdr->rsh_frstr = layout.frstr;
    hdr->rsh_frimg = layout.frimg;
    hdr->rsh_string = layout.raw + disk->hdr.rsh_string;
    hdr->rsh_imdata = layout.raw + disk->hdr.rsh_imdata;
    memcpy(image + layout.raw, disk->base, disk->size);

    if (!disk_range(disk, disk->hdr.rsh_object, (LONG)hdr->rsh_nobs * DISK_OBJECT_SIZE)
        || !disk_range(disk, disk->hdr.rsh_tedinfo, (LONG)hdr->rsh_nted * DISK_TEDINFO_SIZE)
        || !disk_range(disk, disk->hdr.rsh_iconblk, (LONG)hdr->rsh_nib * DISK_ICONBLK_SIZE)
        || !disk_range(disk, disk->hdr.rsh_bitblk, (LONG)hdr->rsh_nbb * DISK_BITBLK_SIZE)
        || !disk_range(disk, disk->hdr.rsh_trindex, (LONG)hdr->rsh_ntree * 4L)
        || !disk_range(disk, disk->hdr.rsh_frstr, (LONG)hdr->rsh_nstring * 4L)
        || !disk_range(disk, disk->hdr.rsh_frimg, (LONG)hdr->rsh_nimages * 4L))
        goto fail;

    for (i = 0; i < hdr->rsh_nted; i++)
    {
        off = disk->hdr.rsh_tedinfo + i*DISK_TEDINFO_SIZE;
        ted = (TEDINFO *)(image + layout.tedinfo + i*sizeof(TEDINFO));
        spec = disk_ulong(disk, off);
        if (!disk_string(disk, spec)) goto fail;
        ted->te_ptext = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec);
        spec = disk_ulong(disk, off+4L);
        if (!disk_string(disk, spec)) goto fail;
        ted->te_ptmplt = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec);
        spec = disk_ulong(disk, off+8L);
        if (!disk_string(disk, spec)) goto fail;
        ted->te_pvalid = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec);
        ted->te_font = disk_word(disk, off+12L); ted->te_junk1 = disk_word(disk, off+14L);
        ted->te_just = disk_word(disk, off+16L); ted->te_color = disk_word(disk, off+18L);
        ted->te_junk2 = disk_word(disk, off+20L); ted->te_thickness = disk_word(disk, off+22L);
        ted->te_txtlen = disk_word(disk, off+24L); ted->te_tmplen = disk_word(disk, off+26L);
    }
    for (i = 0; i < hdr->rsh_nib; i++)
    {
        off = disk->hdr.rsh_iconblk + i*DISK_ICONBLK_SIZE;
        icon = (ICONBLK *)(image + layout.iconblk + i*sizeof(ICONBLK));
        icon->ib_wicon = disk_word(disk, off+22L); icon->ib_hicon = disk_word(disk, off+24L);
        words = (LONG)(icon->ib_wicon/16) * icon->ib_hicon;
        spec = disk_ulong(disk, off);
        if (!native_disk_ptr(image, &layout, disk, spec) || !copy_disk_words((WORD *)(image + layout.raw + spec), disk, spec, words)) goto fail;
        icon->ib_pmask = (WORD *)(image + layout.raw + spec);
        spec = disk_ulong(disk, off+4L);
        if (!native_disk_ptr(image, &layout, disk, spec) || !copy_disk_words((WORD *)(image + layout.raw + spec), disk, spec, words)) goto fail;
        icon->ib_pdata = (WORD *)(image + layout.raw + spec);
        spec = disk_ulong(disk, off+8L);
        if (!disk_string(disk, spec)) goto fail;
        icon->ib_ptext = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec);
        icon->ib_char = disk_word(disk, off+12L); icon->ib_xchar = disk_word(disk, off+14L);
        icon->ib_ychar = disk_word(disk, off+16L); icon->ib_xicon = disk_word(disk, off+18L);
        icon->ib_yicon = disk_word(disk, off+20L); icon->ib_xtext = disk_word(disk, off+26L);
        icon->ib_ytext = disk_word(disk, off+28L); icon->ib_wtext = disk_word(disk, off+30L);
        icon->ib_htext = disk_word(disk, off+32L);
    }
    for (i = 0; i < hdr->rsh_nbb; i++)
    {
        off = disk->hdr.rsh_bitblk + i*DISK_BITBLK_SIZE;
        bit = (BITBLK *)(image + layout.bitblk + i*sizeof(BITBLK));
        bit->bi_wb = disk_word(disk, off+4L); bit->bi_hl = disk_word(disk, off+6L);
        words = ((LONG)bit->bi_wb * bit->bi_hl) / 2L;
        spec = disk_ulong(disk, off);
        if (!native_disk_ptr(image, &layout, disk, spec) || !copy_disk_words((WORD *)(image + layout.raw + spec), disk, spec, words)) goto fail;
        bit->bi_pdata = image + layout.raw + spec;
        bit->bi_x = disk_word(disk, off+8L); bit->bi_y = disk_word(disk, off+10L); bit->bi_color = disk_word(disk, off+12L);
    }
    for (i = 0; i < hdr->rsh_nobs; i++)
    {
        off = disk->hdr.rsh_object + i*DISK_OBJECT_SIZE;
        obj = (OBJECT *)(image + layout.object + i*sizeof(OBJECT));
        obj->ob_next = disk_word(disk, off); obj->ob_head = disk_word(disk, off+2L); obj->ob_tail = disk_word(disk, off+4L);
        obj->ob_type = disk_uword(disk, off+6L); obj->ob_flags = disk_uword(disk, off+8L); obj->ob_state = disk_uword(disk, off+10L);
        spec = disk_ulong(disk, off+12L);
        if (obj->ob_flags & INDIRECT)
        {
            if ((spec & 3L) || !disk_range(disk, spec, 4L)) goto fail;
            *(LONG *)(image + layout.raw + spec) = (LONG)disk_ulong(disk, spec);
            obj->ob_spec.indirect = (OBSPEC *)(image + layout.raw + spec);
        }
        else switch (obj->ob_type & 0x00ff)
        {
        case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT:
            obj->ob_spec.tedinfo = native_tedinfo_ptr(image, &layout, disk, spec); if (!obj->ob_spec.tedinfo) goto fail; break;
        case G_IMAGE:
            obj->ob_spec.bitblk = native_bitblk_ptr(image, &layout, disk, spec); if (!obj->ob_spec.bitblk) goto fail; break;
        case G_ICON:
            obj->ob_spec.iconblk = native_iconblk_ptr(image, &layout, disk, spec); if (!obj->ob_spec.iconblk) goto fail; break;
        case G_STRING: case G_BUTTON: case G_TITLE:
            if (!disk_string(disk, spec)) goto fail;
            obj->ob_spec.free_string = native_disk_ptr(image, &layout, disk, spec); if (!obj->ob_spec.free_string) goto fail; break;
        default: obj->ob_spec.index = (LONG)spec; break;
        }
        obj->ob_x = disk_word(disk, off+16L); obj->ob_y = disk_word(disk, off+18L);
        obj->ob_width = disk_word(disk, off+20L); obj->ob_height = disk_word(disk, off+22L);
    }
    trees = (OBJECT **)(image + layout.trindex);
    for (i = 0; i < hdr->rsh_ntree; i++)
    {
        spec = disk_ulong(disk, disk->hdr.rsh_trindex + i*4L);
        if ((spec < disk->hdr.rsh_object) || ((spec-disk->hdr.rsh_object) % DISK_OBJECT_SIZE)
            || (spec >= (ULONG)(disk->hdr.rsh_object + (LONG)hdr->rsh_nobs*DISK_OBJECT_SIZE))) goto fail;
        trees[i] = (OBJECT *)(image + layout.object + ((spec-disk->hdr.rsh_object)/DISK_OBJECT_SIZE)*sizeof(OBJECT));
    }
    frstr = (BYTE **)(image + layout.frstr);
    for (i = 0; i < hdr->rsh_nstring; i++) { spec = disk_ulong(disk, disk->hdr.rsh_frstr+i*4L); if (!disk_string(disk, spec)) goto fail; frstr[i] = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec); }
    frimg = (void **)(image + layout.frimg);
    for (i = 0; i < hdr->rsh_nimages; i++) { spec = disk_ulong(disk, disk->hdr.rsh_frimg+i*4L); frimg[i] = native_disk_ptr(image, &layout, disk, spec); if (!frimg[i]) goto fail; }
    pglobal->ap_rscmem = hdr;
    pglobal->ap_rsclen = layout.total;
    pglobal->ap_ptree = trees;
    return TRUE;
fail:
    dos_free(image);
    return FALSE;
}

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
    if (free_cicon_buffers(rs_hdr) < 0)
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
 *  Read resource file into memory and fix everything up except the
 *  x,y,w,h, parts which depend upon a GSX open workstation.  In the
 *  case of the GEM resource file this workstation will not have
 *  been loaded into memory yet.
 */
static WORD rs_readit(AESGLOBAL *pglobal,UWORD fd)
{
    UBYTE header[DISK_RSHDR_SIZE];
    struct disk_rsc disk;
    UBYTE *buffer;
    LONG rslsize;

    /* read the header */
    if (dos_read(fd, DISK_RSHDR_SIZE, header) != DISK_RSHDR_SIZE)
        return FALSE;           /* error or short read */
    disk.base = header;
    disk.size = DISK_RSHDR_SIZE;
    disk_decode_rshdr(&disk);
    rslsize = disk.hdr.rsh_rssize;
    if (disk.hdr.rsh_vrsn & NEW_FORMAT_RSC)
    {
        if (dos_lseek(fd, 0, rslsize) < 0L)
            return FALSE;
        if (dos_read(fd, sizeof(ULONG), header) != sizeof(ULONG))
            return FALSE;
        disk.base = header;
        rslsize = (LONG)disk_ulong(&disk, 0L);
    }
    if (rslsize < DISK_RSHDR_SIZE)
        return FALSE;
    buffer = dos_alloc_anyram(rslsize);
    if (!buffer)
        return FALSE;
    /* read it all in */
    if (dos_lseek(fd, 0, 0x0L) < 0L)    /* mode 0: absolute offset */
        goto fail;
    if (dos_read(fd, rslsize, buffer) != rslsize)
        goto fail;               /* error or short read */
    if (!disk_header(&disk, buffer, rslsize))
        goto fail;
    if (!materialize_rsc(pglobal, &disk))
        goto fail;
    dos_free(buffer);
    return TRUE;
fail:
    dos_free(buffer);
    return FALSE;
}


static AESGLOBAL rs_own_global;

/*
 *  rs_loadmem: load a resource image already held in memory
 *
 *  rsmem must be a complete 'new format' resource file image -- the same
 *  bytes rsrc_load() would read from disk; it is copied to freshly
 *  allocated memory and parsed exactly like rs_readit() does, then fixed
 *  up like rs_load() (including fix_objects(), which wires G_CICON
 *  ob_spec's to their CICONBLKs).  Returns the root tree, or NULL.
 *
 *  pglobal may be NULL: an internal AESGLOBAL is then used.  This is the
 *  desktop test hook's path (CONF_WITH_VDI_CICON_TEST) -- it has no
 *  application context of its own.
 */
OBJECT *rs_loadmem(AESGLOBAL *pglobal, const void *rsmem, LONG size)
{
    struct disk_rsc disk;

    if (!pglobal)
        pglobal = &rs_own_global;
    if (!disk_header(&disk, rsmem, size) || !materialize_rsc(pglobal, &disk))
        return NULL;
    rs_fixit(pglobal);

    return pglobal->ap_ptree[0];
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
