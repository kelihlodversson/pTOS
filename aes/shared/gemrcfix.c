/* Native-endian, native-structure GEM resource relocation.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gemrcfix.h"
#include "obdefs.h"
#include "string.h"

static BOOL region(const RSHDR *h, uintptr_t offset, size_t bytes, size_t alignment)
{
    return offset >= sizeof(*h) && offset <= h->rsh_rssize &&
        bytes <= h->rsh_rssize-offset && !(offset % alignment);
}
static BOOL table(const RSHDR *h, UWORD offset, WORD count, size_t size, size_t alignment)
{
    return count >= 0 && (!count || region(h,offset,(size_t)count*size,alignment));
}
static BOOL entry(const RSHDR *h, uintptr_t offset, UWORD start, WORD count, size_t size)
{
    return offset >= start && offset-start < (size_t)count*size && !((offset-start)%size) &&
        region(h,offset,size,1);
}
/* Pointer payloads cannot overlap structural tables that relocation edits. */
static BOOL payload(const RSHDR *h, uintptr_t offset, size_t bytes, size_t alignment)
{
    if (!region(h,offset,bytes,alignment)) return FALSE;
#define CLEAR(field,count,type) (!(h->count) || offset+bytes <= h->field || \
    offset >= h->field+(size_t)h->count*sizeof(type))
    return CLEAR(rsh_object,rsh_nobs,OBJECT) && CLEAR(rsh_tedinfo,rsh_nted,TEDINFO) &&
        CLEAR(rsh_iconblk,rsh_nib,ICONBLK) && CLEAR(rsh_bitblk,rsh_nbb,BITBLK) &&
        CLEAR(rsh_trindex,rsh_ntree,void *) && CLEAR(rsh_frstr,rsh_nstring,void *) &&
        CLEAR(rsh_frimg,rsh_nimages,void *);
#undef CLEAR
}
static BOOL text(const RSHDR *h, uintptr_t offset, WORD capacity)
{
    const char *p;
    size_t length, used = 0;
    if (offset == (uintptr_t)-1) return TRUE;
    if (capacity < 0 || !region(h,offset,capacity ? capacity : 1,1)) return FALSE;
    p = (const char *)h+offset;
    length = capacity ? (size_t)capacity : h->rsh_rssize-offset;
    while (length--) {
        used++;
        if (!*p++) return payload(h,offset,capacity ? (size_t)capacity : used,1);
    }
    return FALSE;
}
static void *relocated(const RSHDR *h, uintptr_t offset)
{
    return offset == (uintptr_t)-1 ? (void *)-1L : (char *)h+offset;
}
static BOOL spec(const RSHDR *h, const OBSPEC *from, OBSPEC *to, UWORD type)
{
    uintptr_t offset;
    /* Read a complete native slot, including wide-host offset fixtures. */
    memcpy(&offset,from,sizeof(offset));
    switch (type & 0xff) {
    case G_BOX: case G_IBOX: case G_BOXCHAR: return TRUE;
    case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT:
        if (!entry(h,offset,h->rsh_tedinfo,h->rsh_nted,sizeof(TEDINFO))) return FALSE;
        break;
    case G_ICON:
        if (!entry(h,offset,h->rsh_iconblk,h->rsh_nib,sizeof(ICONBLK))) return FALSE;
        break;
    case G_IMAGE:
        if (!entry(h,offset,h->rsh_bitblk,h->rsh_nbb,sizeof(BITBLK))) return FALSE;
        break;
    case G_USERDEF:
        if (!payload(h,offset,sizeof(USERBLK),__alignof__(USERBLK))) return FALSE;
        break;
    case G_STRING: case G_BUTTON: case G_TITLE:
        if (!text(h,offset,0)) return FALSE;
        break;
    default: return FALSE; /* Colour/extended resources are a separate format. */
    }
    to->indirect = relocated(h,offset);
    return TRUE;
}

BOOL rs_rcfix_native(const RSHDR *h, RSHDR *out)
{
    const OBJECT *objects;
    const TEDINFO *ted;
    const ICONBLK *icon;
    const BITBLK *bit;
    OBJECT *target;
    TEDINFO *tt;
    ICONBLK *ti;
    BITBLK *tb;
    void *const *p;
    void **q;
    uintptr_t offset;
    size_t bytes;
    WORD i, j;
    struct { size_t start, end; } spans[7];
    if (h->rsh_rssize < sizeof(*h) || (h->rsh_vrsn & ~1)) return FALSE;
#define TABLE(field,count,type) table(h,h->field,h->count,sizeof(type),__alignof__(type))
    if (!TABLE(rsh_object,rsh_nobs,OBJECT) || !TABLE(rsh_tedinfo,rsh_nted,TEDINFO) ||
        !TABLE(rsh_iconblk,rsh_nib,ICONBLK) || !TABLE(rsh_bitblk,rsh_nbb,BITBLK) ||
        !TABLE(rsh_trindex,rsh_ntree,void *) || !TABLE(rsh_frstr,rsh_nstring,void *) ||
        !TABLE(rsh_frimg,rsh_nimages,void *)) return FALSE;
#undef TABLE
#define SPAN(n,field,count,type) spans[n].start = h->field; \
    spans[n].end = h->field+(size_t)h->count*sizeof(type)
    SPAN(0,rsh_object,rsh_nobs,OBJECT); SPAN(1,rsh_tedinfo,rsh_nted,TEDINFO);
    SPAN(2,rsh_iconblk,rsh_nib,ICONBLK); SPAN(3,rsh_bitblk,rsh_nbb,BITBLK);
    SPAN(4,rsh_trindex,rsh_ntree,void *); SPAN(5,rsh_frstr,rsh_nstring,void *);
    SPAN(6,rsh_frimg,rsh_nimages,void *);
#undef SPAN
    for (i = 0; i < 7; i++) for (j = 0; j < i; j++)
        if (spans[i].start != spans[i].end && spans[j].start != spans[j].end &&
            spans[i].start < spans[j].end && spans[j].start < spans[i].end) return FALSE;
    objects = (const OBJECT *)((const char *)h+h->rsh_object);
    target = (OBJECT *)((char *)out+h->rsh_object);
    for (i = 0; i < h->rsh_nobs; i++) {
        if (objects[i].ob_flags & INDIRECT) {
            offset = (uintptr_t)objects[i].ob_spec.indirect;
            if (!payload(h,offset,sizeof(OBSPEC),__alignof__(OBSPEC)) ||
                !spec(h,(const OBSPEC *)((const char *)h+offset),
                      (OBSPEC *)((char *)out+offset),objects[i].ob_type)) return FALSE;
            target[i].ob_spec.indirect = relocated(h,offset);
        } else if (!spec(h,&objects[i].ob_spec,&target[i].ob_spec,objects[i].ob_type)) return FALSE;
    }
    ted = (const TEDINFO *)((const char *)h+h->rsh_tedinfo);
    tt = (TEDINFO *)((char *)out+h->rsh_tedinfo);
    for (i = 0; i < h->rsh_nted; i++) {
        if (!text(h,(uintptr_t)ted[i].te_ptext,ted[i].te_txtlen) ||
            !text(h,(uintptr_t)ted[i].te_ptmplt,ted[i].te_tmplen) ||
            !text(h,(uintptr_t)ted[i].te_pvalid,0)) return FALSE;
        tt[i].te_ptext = relocated(h,(uintptr_t)ted[i].te_ptext);
        tt[i].te_ptmplt = relocated(h,(uintptr_t)ted[i].te_ptmplt);
        tt[i].te_pvalid = relocated(h,(uintptr_t)ted[i].te_pvalid);
    }
    icon = (const ICONBLK *)((const char *)h+h->rsh_iconblk);
    ti = (ICONBLK *)((char *)out+h->rsh_iconblk);
    for (i = 0; i < h->rsh_nib; i++) {
        if (icon[i].ib_wicon < 0 || icon[i].ib_hicon < 0) return FALSE;
        bytes = ((size_t)icon[i].ib_wicon+15)/16*icon[i].ib_hicon*2;
        if (!payload(h,(uintptr_t)icon[i].ib_pmask,bytes,2) ||
            !payload(h,(uintptr_t)icon[i].ib_pdata,bytes,2) ||
            !text(h,(uintptr_t)icon[i].ib_ptext,0)) return FALSE;
        ti[i].ib_pmask = relocated(h,(uintptr_t)icon[i].ib_pmask);
        ti[i].ib_pdata = relocated(h,(uintptr_t)icon[i].ib_pdata);
        ti[i].ib_ptext = relocated(h,(uintptr_t)icon[i].ib_ptext);
    }
    bit = (const BITBLK *)((const char *)h+h->rsh_bitblk);
    tb = (BITBLK *)((char *)out+h->rsh_bitblk);
    for (i = 0; i < h->rsh_nbb; i++) {
        if (bit[i].bi_wb < 0 || (bit[i].bi_wb & 1) || bit[i].bi_hl < 0) return FALSE;
        bytes = (size_t)bit[i].bi_wb*bit[i].bi_hl;
        if (!payload(h,(uintptr_t)bit[i].bi_pdata,bytes,2)) return FALSE;
        tb[i].bi_pdata = relocated(h,(uintptr_t)bit[i].bi_pdata);
    }
    p = (void *const *)((const char *)h+h->rsh_trindex);
    q = (void **)((char *)out+h->rsh_trindex);
    for (i = 0; i < h->rsh_ntree; i++) {
        offset = (uintptr_t)p[i];
        if (!entry(h,offset,h->rsh_object,h->rsh_nobs,sizeof(OBJECT))) return FALSE;
        q[i] = relocated(h,offset);
    }
    p = (void *const *)((const char *)h+h->rsh_frstr);
    q = (void **)((char *)out+h->rsh_frstr);
    for (i = 0; i < h->rsh_nstring; i++) {
        if (!text(h,(uintptr_t)p[i],0)) return FALSE;
        q[i] = relocated(h,(uintptr_t)p[i]);
    }
    p = (void *const *)((const char *)h+h->rsh_frimg);
    q = (void **)((char *)out+h->rsh_frimg);
    for (i = 0; i < h->rsh_nimages; i++) {
        offset = (uintptr_t)p[i];
        if (!entry(h,offset,h->rsh_bitblk,h->rsh_nbb,sizeof(BITBLK))) return FALSE;
        q[i] = relocated(h,offset);
    }
    return TRUE;
}
