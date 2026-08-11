/*
 *  rsload_portable.c - portable canonical RSC loader
 *
 *  The bounds-checked big-endian disk parser plus a native materializer,
 *  with rs_loadmem().  Selected by build.mk whenever CONF_WITH_LEGACY_RSC_LOAD
 *  is off (CONF_WITH_PORTABLE_RSC_LOAD).
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
#include "endian.h"
#include "string.h"

#include "rsload.h"

/* RSC records on disk are always big-endian and have these fixed sizes. */
#define DISK_RSHDR_SIZE     36
#define DISK_OBJECT_SIZE    24
#define DISK_TEDINFO_SIZE   28
#define DISK_ICONBLK_SIZE   34
#define DISK_BITBLK_SIZE    14
#define DISK_CICONBLK_SIZE  38
#define DISK_CICON_SIZE     22
#define DISK_USERBLK_SIZE   8

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

#define D_CICONBLK_MONOBLK  0
#define D_CICONBLK_MAINLIST 34

#define D_CICON_PLANES      0
#define D_CICON_COL_DATA    2
#define D_CICON_COL_MASK    6
#define D_CICON_SEL_DATA    10
#define D_CICON_SEL_MASK    14
#define D_CICON_NEXT_RES    18

#define D_USERBLK_CODE      0
#define D_USERBLK_PARM      4

struct disk_rsc {
    const UBYTE *base;
    LONG size;
    RSHDR hdr;
};

struct native_rsc_layout {
    LONG object, tedinfo, iconblk, bitblk;
    LONG userblk, trindex, frstr, frimg;
    LONG extension, cicon_table, ciconblks, cicons, raw, total;
    LONG nuserblk;
};

struct disk_cicon_info {
    LONG table;
    LONG ciconblks;
    LONG cicons;
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

static BOOL userblk_offset(const struct disk_rsc *disk, ULONG spec, LONG *index)
{
    LONG i, off;
    ULONG value;
    BOOL found;

    if (!disk_range(disk, spec, DISK_USERBLK_SIZE))
        return FALSE;
    *index = 0;
    found = FALSE;
    for (i = 0; i < disk->hdr.rsh_nobs; i++)
    {
        off = disk->hdr.rsh_object + i*DISK_OBJECT_SIZE;
        if ((disk_uword(disk, off+D_OBJ_TYPE) & 0x00ff) != G_USERDEF)
            continue;
        value = disk_ulong(disk, off+D_OBJ_SPEC);
        if (disk_uword(disk, off+D_OBJ_FLAGS) & INDIRECT)
        {
            if (!disk_range(disk, value, sizeof(ULONG)))
                return FALSE;
            value = disk_ulong(disk, value);
        }
        if (!disk_range(disk, value, DISK_USERBLK_SIZE))
            return FALSE;
        if (value == spec)
            found = TRUE;
        if (value < spec)
        {
            LONG j;

            for (j = 0; j < i; j++)
            {
                LONG previous = disk->hdr.rsh_object + j*DISK_OBJECT_SIZE;
                ULONG previous_value;

                if ((disk_uword(disk, previous+D_OBJ_TYPE) & 0x00ff) != G_USERDEF)
                    continue;
                previous_value = disk_ulong(disk, previous+D_OBJ_SPEC);
                if (disk_uword(disk, previous+D_OBJ_FLAGS) & INDIRECT)
                {
                    if (!disk_range(disk, previous_value, sizeof(ULONG)))
                        return FALSE;
                    previous_value = disk_ulong(disk, previous_value);
                }
                if (previous_value == value)
                    break;
            }
            if (j == i)
                (*index)++;
        }
    }
    return found;
}

static BOOL scan_disk_ciconblk(const struct disk_rsc *disk, LONG offset,
                               LONG *cicon_count)
{
    LONG words, pos, i, planes, header, payload;
    BOOL selected;
    ULONG count, next;

    if (!disk_range(disk, offset, DISK_CICONBLK_SIZE))
        return FALSE;
    words = (((LONG)disk_word(disk, offset+22L) + 15L) / 16L)
        * disk_word(disk, offset+24L);
    if ((disk_word(disk, offset+22L) < 0) || (disk_word(disk, offset+24L) < 0)
        || (words < 0L) || (words > (disk->size-DISK_CICONBLK_SIZE-12L)/4L))
        return FALSE;
    pos = offset + DISK_CICONBLK_SIZE + words*4L + 12L;
    count = disk_ulong(disk, offset+D_CICONBLK_MAINLIST);
    if (!disk_range(disk, pos, 0L) || (count > 0x7fffL-*cicon_count)
        || (count > (ULONG)((disk->size-pos)/DISK_CICON_SIZE)))
        return FALSE;
    for (i = 0; i < (LONG)count; i++)
    {
        if (!disk_range(disk, pos, DISK_CICON_SIZE))
            return FALSE;
        header = pos;
        planes = disk_uword(disk, header+D_CICON_PLANES);
        if ((planes <= 0L) || (planes > 16L)
            || (words > (disk->size-header-DISK_CICON_SIZE)/(2L*(planes+1L))))
            return FALSE;
        payload = words*2L*(planes+1L);
        pos = header + DISK_CICON_SIZE;
        if (!disk_range(disk, pos, payload))
            return FALSE;
        pos += payload;
        selected = disk_ulong(disk, header+D_CICON_SEL_DATA) != 0L;
        if (selected)
        {
            if (!disk_range(disk, pos, payload))
                return FALSE;
            pos += payload;
        }
        next = disk_ulong(disk, header+D_CICON_NEXT_RES);
        if (next != (ULONG)((i+1L < (LONG)count) ? 1L : 0L))
            return FALSE;
    }
    *cicon_count += count;
    return TRUE;
}

static BOOL scan_disk_cicons(const struct disk_rsc *disk, struct disk_cicon_info *info)
{
    LONG pos;
    ULONG offset;

    info->table = 0L;
    info->ciconblks = 0L;
    info->cicons = 0L;
    if ((disk->hdr.rsh_vrsn & NEW_FORMAT_RSC) == 0)
        return TRUE;
    if (!disk_range(disk, disk->hdr.rsh_rssize, 12L)
        || (disk_ulong(disk, disk->hdr.rsh_rssize) != (ULONG)disk->size))
        return FALSE;
    offset = disk_ulong(disk, disk->hdr.rsh_rssize+4L);
    if ((offset == 0L) || (offset == (ULONG)-1L))
        return TRUE;
    if ((offset & 1L) || !disk_range(disk, offset, 4L))
        return FALSE;
    info->table = offset;
    for (pos = offset; disk_range(disk, pos, 4L); pos += 4L)
    {
        offset = disk_ulong(disk, pos);
        if (offset == (ULONG)-1L)
            return TRUE;
        if (!scan_disk_ciconblk(disk, offset, &info->cicons)
            || (info->ciconblks == 0x7fffL))
            return FALSE;
        info->ciconblks++;
    }
    return FALSE;
}

static BOOL layout_ordinary(const struct disk_rsc *disk, const struct disk_cicon_info *cicons,
                            struct native_rsc_layout *layout)
{
    const RSHDR *hdr = &disk->hdr;
    LONG offset, i, userblk_index;

    if ((hdr->rsh_nobs < 0) || (hdr->rsh_ntree < 0) || (hdr->rsh_nted < 0)
        || (hdr->rsh_nib < 0) || (hdr->rsh_nbb < 0) || (hdr->rsh_nstring < 0)
        || (hdr->rsh_nimages < 0))
        return FALSE;
    if (!disk_range(disk, hdr->rsh_object, (LONG)hdr->rsh_nobs * DISK_OBJECT_SIZE))
        return FALSE;
    layout->nuserblk = 0;
    for (i = 0; i < hdr->rsh_nobs; i++)
    {
        LONG object = hdr->rsh_object + i*DISK_OBJECT_SIZE;

        if ((disk_uword(disk, object+D_OBJ_TYPE) & 0x00ff) == G_CICON)
        {
            ULONG cicon_index = disk_ulong(disk, object+D_OBJ_SPEC);

            if (disk_uword(disk, object+D_OBJ_FLAGS) & INDIRECT)
            {
                if (!disk_range(disk, cicon_index, sizeof(ULONG)))
                    return FALSE;
                cicon_index = disk_ulong(disk, cicon_index);
            }
            if (!cicons->table || (cicon_index >= (ULONG)cicons->ciconblks))
                return FALSE;
        }
        if ((disk_uword(disk, object+D_OBJ_TYPE) & 0x00ff) == G_USERDEF)
        {
            ULONG spec = disk_ulong(disk, object+D_OBJ_SPEC);

            if (disk_uword(disk, object+D_OBJ_FLAGS) & INDIRECT)
            {
                if (!disk_range(disk, spec, sizeof(ULONG)))
                    return FALSE;
                spec = disk_ulong(disk, spec);
            }
            if (!userblk_offset(disk, spec, &userblk_index))
                return FALSE;
            if (userblk_index >= layout->nuserblk)
                layout->nuserblk = userblk_index + 1;
        }
    }
    offset = sizeof(RSHDR);
    layout->object = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nobs * sizeof(OBJECT))) return FALSE;
    layout->tedinfo = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nted * sizeof(TEDINFO))) return FALSE;
    layout->iconblk = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nib * sizeof(ICONBLK))) return FALSE;
    layout->bitblk = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nbb * sizeof(BITBLK))) return FALSE;
    layout->userblk = align_long(offset);
    if (!layout_add(&offset, layout->nuserblk * sizeof(USERBLK))) return FALSE;
    layout->trindex = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_ntree * sizeof(void *))) return FALSE;
    layout->frstr = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nstring * sizeof(void *))) return FALSE;
    layout->frimg = align_long(offset);
    if (!layout_add(&offset, (LONG)hdr->rsh_nimages * sizeof(void *))) return FALSE;
    layout->extension = align_long(offset);
    if (!layout_add(&offset, 3L * sizeof(LONG))) return FALSE;
    layout->cicon_table = align_long(offset);
    if (!layout_add(&offset, (cicons->ciconblks + 1L) * sizeof(CICONBLK *))) return FALSE;
    layout->ciconblks = align_long(offset);
    if (!layout_add(&offset, cicons->ciconblks * sizeof(CICONBLK))) return FALSE;
    layout->cicons = align_long(offset);
    if (!layout_add(&offset, cicons->cicons * sizeof(CICON))) return FALSE;
    layout->raw = align_long(offset);
    if (!layout_add(&offset, disk->size)) return FALSE;
    layout->total = offset;
    return layout->total <= 0xffffL;
}


#if CONF_WITH_COLOUR_ICONS
static void transform_cicons(RSHDR *hdr)
{
    CICONBLK **ciconblkptr, **p;
    LONG num_ciconblks;

    /* find the CICONBLK ptr table & count the CICONBLKs */
    ciconblkptr = get_ciconblkptr(hdr);
    if (!ciconblkptr)   /* yes, we have no CICONBLKs */
        return;

    for (num_ciconblks = 0, p = ciconblkptr; *p != (CICONBLK *)-1L; p++)
        num_ciconblks++;

    transform_all_cicons(num_ciconblks, ciconblkptr);
}
#endif


void fix_objects(void)
{
    WORD ii;
    WORD obtype;
    OBJECT *obj;
#if CONF_WITH_COLOUR_ICONS
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
            spec = (obj->ob_flags & INDIRECT) ? obj->ob_spec.indirect : &obj->ob_spec;
            spec->ciconblk = get_ciconblkptr(rs_hdr)[spec->index];
#endif
            break;
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

static USERBLK *native_userblk_ptr(UBYTE *image, const struct native_rsc_layout *layout,
                                   const struct disk_rsc *disk, ULONG offset)
{
    LONG index;

    if (!userblk_offset(disk, offset, &index))
        return NULL;
    return (USERBLK *)(image + layout->userblk + index*sizeof(USERBLK));
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

static BOOL disk_string_capacity(const struct disk_rsc *disk, ULONG offset, WORD length)
{
    LONG i;

    if (length < 0)
        return FALSE;
    if (offset == (ULONG)-1L)
        return TRUE;
    if (!disk_range(disk, offset, (LONG)length))
        return FALSE;
    for (i = 0; i < length; i++)
        if (!disk->base[offset+i])
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

static BOOL materialize_cicons(UBYTE *image, const struct native_rsc_layout *layout,
                               const struct disk_rsc *disk, const struct disk_cicon_info *info)
{
    LONG pos, table_pos, block, words, count, i, j, header;
    CICONBLK **table;
    CICONBLK *ciconblk;
    CICON *cicon;
    BOOL selected;

    table = (CICONBLK **)(image + layout->cicon_table);
    if (!info->table)
    {
        table[0] = (CICONBLK *)-1L;
        return TRUE;
    }
    cicon = (CICON *)(image + layout->cicons);
    for (i = 0, table_pos = info->table; i < info->ciconblks; i++, table_pos += 4L)
    {
        block = disk_ulong(disk, table_pos);
        ciconblk = (CICONBLK *)(image + layout->ciconblks + i*sizeof(CICONBLK));
        table[i] = ciconblk;
        words = (((LONG)disk_word(disk, block+22L) + 15L) / 16L)
            * disk_word(disk, block+24L);
        ciconblk->monoblk.ib_wicon = disk_word(disk, block+22L);
        ciconblk->monoblk.ib_hicon = disk_word(disk, block+24L);
        ciconblk->monoblk.ib_char = disk_word(disk, block+12L);
        ciconblk->monoblk.ib_xchar = disk_word(disk, block+14L);
        ciconblk->monoblk.ib_ychar = disk_word(disk, block+16L);
        ciconblk->monoblk.ib_xicon = disk_word(disk, block+18L);
        ciconblk->monoblk.ib_yicon = disk_word(disk, block+20L);
        ciconblk->monoblk.ib_xtext = disk_word(disk, block+26L);
        ciconblk->monoblk.ib_ytext = disk_word(disk, block+28L);
        ciconblk->monoblk.ib_wtext = disk_word(disk, block+30L);
        ciconblk->monoblk.ib_htext = disk_word(disk, block+32L);
        pos = block + DISK_CICONBLK_SIZE;
        ciconblk->monoblk.ib_pdata = (WORD *)(image + layout->raw + pos);
        if (!copy_disk_words(ciconblk->monoblk.ib_pdata, disk, pos, words)) return FALSE;
        pos += words*2L;
        ciconblk->monoblk.ib_pmask = (WORD *)(image + layout->raw + pos);
        if (!copy_disk_words(ciconblk->monoblk.ib_pmask, disk, pos, words)) return FALSE;
        pos += words*2L;
        ciconblk->monoblk.ib_ptext = (BYTE *)(image + layout->raw + pos);
        pos += 12L;
        count = disk_ulong(disk, block+D_CICONBLK_MAINLIST);
        ciconblk->mainlist = count ? cicon : NULL;
        for (j = 0; j < count; j++, cicon++)
        {
            header = pos;
            selected = disk_ulong(disk, header+D_CICON_SEL_DATA) != 0L;
            cicon->num_planes = disk_word(disk, header+D_CICON_PLANES);
            pos += DISK_CICON_SIZE;
            cicon->col_data = (WORD *)(image + layout->raw + pos);
            if (!copy_disk_words(cicon->col_data, disk, pos, words*cicon->num_planes)) return FALSE;
            pos += words*cicon->num_planes*2L;
            cicon->col_mask = (WORD *)(image + layout->raw + pos);
            if (!copy_disk_words(cicon->col_mask, disk, pos, words)) return FALSE;
            pos += words*2L;
            cicon->sel_data = NULL;
            cicon->sel_mask = NULL;
            if (selected)
            {
                cicon->sel_data = (WORD *)(image + layout->raw + pos);
                if (!copy_disk_words(cicon->sel_data, disk, pos, words*cicon->num_planes)) return FALSE;
                pos += words*cicon->num_planes*2L;
                cicon->sel_mask = (WORD *)(image + layout->raw + pos);
                if (!copy_disk_words(cicon->sel_mask, disk, pos, words)) return FALSE;
                pos += words*2L;
            }
            cicon->next_res = (j+1L < count) ? cicon+1 : NULL;
        }
    }
    table[info->ciconblks] = (CICONBLK *)-1L;
    return TRUE;
}

static BOOL decode_object_spec(OBSPEC *native, UWORD type, ULONG spec,
                               UBYTE *image, const struct native_rsc_layout *layout,
                               const struct disk_rsc *disk)
{
    switch (type & 0x00ff)
    {
    case G_TEXT:
    case G_BOXTEXT:
    case G_FTEXT:
    case G_FBOXTEXT:
        native->tedinfo = native_tedinfo_ptr(image, layout, disk, spec);
        return native->tedinfo != NULL;
    case G_IMAGE:
        native->bitblk = native_bitblk_ptr(image, layout, disk, spec);
        return native->bitblk != NULL;
    case G_ICON:
        native->iconblk = native_iconblk_ptr(image, layout, disk, spec);
        return native->iconblk != NULL;
    case G_STRING:
    case G_BUTTON:
    case G_TITLE:
        if (!disk_string(disk, spec))
            return FALSE;
        native->free_string = native_disk_ptr(image, layout, disk, spec);
        return native->free_string != NULL;
    case G_USERDEF:
        native->userblk = native_userblk_ptr(image, layout, disk, spec);
        return native->userblk != NULL;
    default:
        native->index = (LONG)spec;
        return TRUE;
    }
}

static BOOL materialize_rsc(AESGLOBAL *pglobal, const struct disk_rsc *disk)
{
    struct native_rsc_layout layout;
    struct disk_cicon_info cicons;
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
    LONG i, off, words, bytes;
    USERBLK *userblk;

    if (!scan_disk_cicons(disk, &cicons) || !layout_ordinary(disk, &cicons, &layout))
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
        || (layout.userblk > 0xffffL)
        || (layout.trindex > 0xffffL) || (layout.frstr > 0xffffL)
        || (layout.frimg > 0xffffL) || (layout.extension > 0xffffL)
        || (layout.cicon_table > 0xffffL) || (layout.ciconblks > 0xffffL)
        || (layout.cicons > 0xffffL) || (layout.raw + disk->hdr.rsh_string > 0xffffL)
        || (layout.raw + disk->hdr.rsh_imdata > 0xffffL))
        goto fail;
    hdr->rsh_object = layout.object;
    hdr->rsh_tedinfo = layout.tedinfo;
    hdr->rsh_iconblk = layout.iconblk;
    hdr->rsh_bitblk = layout.bitblk;
    hdr->rsh_trindex = layout.trindex;
    hdr->rsh_frstr = layout.frstr;
    hdr->rsh_frimg = layout.frimg;
    hdr->rsh_rssize = layout.extension;
    hdr->rsh_string = layout.raw + disk->hdr.rsh_string;
    hdr->rsh_imdata = layout.raw + disk->hdr.rsh_imdata;
    memcpy(image + layout.raw, disk->base, disk->size);
    ((LONG *)(image + layout.extension))[0] = layout.total;
    ((LONG *)(image + layout.extension))[1] = cicons.table ? layout.cicon_table : 0L;
    ((LONG *)(image + layout.extension))[2] = 0L;
    if (!materialize_cicons(image, &layout, disk, &cicons))
        goto fail;

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
        ted->te_txtlen = disk_word(disk, off+24L);
        if (!disk_string_capacity(disk, spec, ted->te_txtlen)) goto fail;
        ted->te_ptext = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec);
        spec = disk_ulong(disk, off+4L);
        ted->te_tmplen = disk_word(disk, off+26L);
        if (!disk_string_capacity(disk, spec, ted->te_tmplen)) goto fail;
        ted->te_ptmplt = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec);
        spec = disk_ulong(disk, off+8L);
        if (!disk_string(disk, spec)) goto fail;
        ted->te_pvalid = (spec == (ULONG)-1L) ? (BYTE *)-1L : native_disk_ptr(image, &layout, disk, spec);
        ted->te_font = disk_word(disk, off+12L); ted->te_junk1 = disk_word(disk, off+14L);
        ted->te_just = disk_word(disk, off+16L); ted->te_color = disk_word(disk, off+18L);
        ted->te_junk2 = disk_word(disk, off+20L); ted->te_thickness = disk_word(disk, off+22L);
    }
    for (i = 0; i < hdr->rsh_nib; i++)
    {
        off = disk->hdr.rsh_iconblk + i*DISK_ICONBLK_SIZE;
        icon = (ICONBLK *)(image + layout.iconblk + i*sizeof(ICONBLK));
        icon->ib_wicon = disk_word(disk, off+22L); icon->ib_hicon = disk_word(disk, off+24L);
        if ((icon->ib_wicon < 0) || (icon->ib_hicon < 0)) goto fail;
        words = (((LONG)icon->ib_wicon + 15L) / 16L) * icon->ib_hicon;
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
        bytes = (LONG)bit->bi_wb * bit->bi_hl;
        if ((bit->bi_wb < 0) || (bit->bi_hl < 0) || (bytes < 0L) || (bytes & 1L)) goto fail;
        words = bytes / 2L;
        spec = disk_ulong(disk, off);
        if (!native_disk_ptr(image, &layout, disk, spec) || !copy_disk_words((WORD *)(image + layout.raw + spec), disk, spec, words)) goto fail;
        bit->bi_pdata = image + layout.raw + spec;
        bit->bi_x = disk_word(disk, off+8L); bit->bi_y = disk_word(disk, off+10L); bit->bi_color = disk_word(disk, off+12L);
    }
    for (i = 0; i < hdr->rsh_nobs; i++)
    {
        off = disk->hdr.rsh_object + i*DISK_OBJECT_SIZE;
        if ((disk_uword(disk, off+D_OBJ_TYPE) & 0x00ff) != G_USERDEF)
            continue;
        spec = disk_ulong(disk, off+D_OBJ_SPEC);
        if (disk_uword(disk, off+D_OBJ_FLAGS) & INDIRECT)
            spec = disk_ulong(disk, spec);
        userblk = native_userblk_ptr(image, &layout, disk, spec);
        if (!userblk) goto fail;
        userblk->ub_code = NULL;
        userblk->ub_parm = (LONG)disk_ulong(disk, spec+D_USERBLK_PARM);
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
            obj->ob_spec.indirect = (OBSPEC *)(image + layout.raw + spec);
            if (!decode_object_spec(obj->ob_spec.indirect, obj->ob_type,
                                    disk_ulong(disk, spec), image, &layout, disk)) goto fail;
        }
        else if (!decode_object_spec(&obj->ob_spec, obj->ob_type, spec, image, &layout, disk)) goto fail;
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
    for (i = 0; i < hdr->rsh_nimages; i++)
    {
        spec = disk_ulong(disk, disk->hdr.rsh_frimg+i*4L);
        frimg[i] = native_bitblk_ptr(image, &layout, disk, spec);
        if (!frimg[i]) goto fail;
    }
#if CONF_WITH_COLOUR_ICONS
    transform_cicons(hdr);
#endif
    pglobal->ap_rscmem = hdr;
    pglobal->ap_rsclen = layout.total;
    pglobal->ap_ptree = trees;
    return TRUE;
fail:
    dos_free(image);
    return FALSE;
}


WORD rs_readit(AESGLOBAL *pglobal,UWORD fd)
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
    if (!disk_header(&disk, rsmem, size) || (disk.hdr.rsh_ntree <= 0)
        || !materialize_rsc(pglobal, &disk))
        return NULL;
    rs_fixit(pglobal);

    return pglobal->ap_ptree[0];
}
