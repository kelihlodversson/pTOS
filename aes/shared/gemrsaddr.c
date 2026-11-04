/* Original GEM resource address lookup, shared with caller-local libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "gemrslib.h"
#include "rsload.h"
#include "intmath.h"
#ifdef AES_RSC_BINDINGS
#include AES_RSC_BINDINGS
#endif
#ifndef RS_ADDR_SLOT_SIZE
#define RS_ADDR_SLOT_SIZE sizeof(LONG)
#endif

void *get_sub(UWORD rsindex, UWORD offset, UWORD rsize)
{
    /* get base of objects and then index in */
    return (char *)rs_hdr + offset + muls(rsize, rsindex);
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
        return *((void **)get_sub(rsindex, hdr->rsh_frstr, RS_ADDR_SLOT_SIZE));
    case R_IMAGEDATA:
        return *((void **)get_sub(rsindex, hdr->rsh_frimg, RS_ADDR_SLOT_SIZE));
    case R_FRSTR:
        offset = hdr->rsh_frstr;
        size = RS_ADDR_SLOT_SIZE;
        break;
    case R_FRIMG:
        offset = hdr->rsh_frimg;
        size = RS_ADDR_SLOT_SIZE;
        break;
    default:
        return (void *)-1L;
    }

    return get_sub(rsindex, offset, size);
} /* get_addr() */


