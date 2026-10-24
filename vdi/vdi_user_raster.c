/*
 * vdi_user_raster.c - bounded caller-local memory raster operations
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "vdi_service.h"
#include "gemerror.h"

static UWORD packed_bytes(const VDI_USER_FDB *form)
{
    return !form->fd_stand && form->fd_nplanes >= 16 ? form->fd_nplanes / 8 : 0;
}

static void order(WORD *a, WORD *b)
{
    WORD temporary;

    if (*a > *b) {
        temporary = *a;
        *a = *b;
        *b = temporary;
    }
}

LONG vdi_user_raster_shape(VDI_SERVICE_RASTER_REQUEST *r,
                           ULONG *source_bytes, ULONG *destination_bytes)
{
    int i;

    *source_bytes = *destination_bytes = 0;
    if (r->reserved)
        return EINVAL;
    if (r->operation != 109 && r->operation != 110 && r->operation != 121)
        return EINVFN;
    /* Framebuffer surfaces need a separate display-service contract. */
    if (!r->source.fd_addr || !r->destination.fd_addr)
        return EINVFN;
    if (r->operation == 110) {
        if (r->source.fd_nplanes > 8)
            return EINVFN;
        if (r->source.fd_w != r->destination.fd_w ||
            r->source.fd_h != r->destination.fd_h ||
            r->source.fd_wdwidth != r->destination.fd_wdwidth ||
            r->source.fd_nplanes != r->destination.fd_nplanes)
            return EINVAL;
    } else {
        if (r->operation == 109) {
            if (r->mode < 0 || r->mode > 15 ||
                r->source.fd_nplanes != r->destination.fd_nplanes)
                return EINVAL;
        } else {
            /* Monochrome transparent copies have an unambiguous native
             * pen representation. Colour conversion is not inferred. */
            if (r->source.fd_nplanes != 1 || r->destination.fd_nplanes != 1)
                return EINVFN;
            if (r->mode < 1 || r->mode > 4 || r->foreground < 0 ||
                r->foreground > 1 || r->background < 0 || r->background > 1)
                return EINVAL;
        }
        for (i = 0; i < 8; i += 4) {
            order(&r->xy[i], &r->xy[i + 2]);
            order(&r->xy[i + 1], &r->xy[i + 3]);
        }
        if (r->xy[0] < 0 || r->xy[1] < 0 || r->xy[4] < 0 || r->xy[5] < 0 ||
            r->xy[2] >= r->source.fd_w || r->xy[3] >= r->source.fd_h ||
            r->xy[6] >= r->destination.fd_w || r->xy[7] >= r->destination.fd_h ||
            r->xy[2] - r->xy[0] != r->xy[6] - r->xy[4] ||
            r->xy[3] - r->xy[1] != r->xy[7] - r->xy[5])
            return EINVAL;
    }
    if (!vdi_user_bitmap_bytes(&r->source, packed_bytes(&r->source), source_bytes) ||
        !vdi_user_bitmap_bytes(&r->destination, packed_bytes(&r->destination), destination_bytes) ||
        *source_bytes > VDI_SERVICE_MAX_BITMAP || *destination_bytes > VDI_SERVICE_MAX_BITMAP)
        return EINVAL;
    if (r->operation == 110)
        r->destination.fd_stand = !r->source.fd_stand;
    return E_OK;
}

static ULONG word_index(const VDI_USER_FDB *form, ULONG xword, ULONG y, ULONG plane)
{
    ULONG at = y * (UWORD)form->fd_wdwidth + xword;

    return form->fd_stand ? plane * (UWORD)form->fd_h * (UWORD)form->fd_wdwidth + at :
                            at * (UWORD)form->fd_nplanes + plane;
}

static ULONG pixel(const VDI_USER_FDB *form, const UBYTE *data, ULONG x, ULONG y)
{
    UWORD bpp = packed_bytes(form);
    ULONG at, value = 0, plane;

    if (bpp) {
        at = (y * (UWORD)form->fd_w + x) * bpp;
        for (plane = 0; plane < bpp; plane++)
            value |= (ULONG)data[at + plane] << (plane * 8);
    } else {
        for (plane = 0; plane < (UWORD)form->fd_nplanes; plane++)
            if (((const UWORD *)data)[word_index(form, x / 16, y, plane)] & (0x8000U >> (x & 15)))
                value |= 1UL << plane;
    }
    return value;
}

static void put_pixel(const VDI_USER_FDB *form, UBYTE *data, ULONG x, ULONG y, ULONG value)
{
    UWORD bpp = packed_bytes(form), bit;
    ULONG at, plane;

    if (bpp) {
        at = (y * (UWORD)form->fd_w + x) * bpp;
        for (plane = 0; plane < bpp; plane++)
            data[at + plane] = value >> (plane * 8);
    } else {
        bit = 0x8000U >> (x & 15);
        for (plane = 0; plane < (UWORD)form->fd_nplanes; plane++) {
            at = word_index(form, x / 16, y, plane);
            if (value & (1UL << plane))
                ((UWORD *)data)[at] |= bit;
            else
                ((UWORD *)data)[at] &= ~bit;
        }
    }
}

void vdi_user_raster_execute(const VDI_SERVICE_RASTER_REQUEST *r,
                             const UBYTE *source, UBYTE *destination)
{
    ULONG x, y, plane, s, d, value, mask;

    if (r->operation == 110) {
        for (plane = 0; plane < (UWORD)r->source.fd_nplanes; plane++)
            for (y = 0; y < (UWORD)r->source.fd_h; y++)
                for (x = 0; x < (UWORD)r->source.fd_wdwidth; x++)
                    ((UWORD *)destination)[word_index(&r->destination, x, y, plane)] =
                        ((const UWORD *)source)[word_index(&r->source, x, y, plane)];
        return;
    }
    mask = r->destination.fd_nplanes == 32 ? 0xffffffffUL : (1UL << r->destination.fd_nplanes) - 1;
    for (y = 0; y <= (ULONG)(r->xy[3] - r->xy[1]); y++) {
        for (x = 0; x <= (ULONG)(r->xy[2] - r->xy[0]); x++) {
            s = pixel(&r->source, source, r->xy[0] + x, r->xy[1] + y);
            d = pixel(&r->destination, destination, r->xy[4] + x, r->xy[5] + y);
            if (r->operation == 109) {
                value = ((r->mode & 1) ? s & d : 0) |
                        ((r->mode & 2) ? s & ~d : 0) |
                        ((r->mode & 4) ? ~s & d : 0) |
                        ((r->mode & 8) ? ~s & ~d : 0);
            } else {
                value = d;
                switch (r->mode) {
                case 1: value = s ? r->foreground : r->background; break;
                case 2: if (s) value = r->foreground; break;
                case 3: if (s) value = d ^ mask; break;
                case 4: if (!s) value = r->background; break;
                }
            }
            put_pixel(&r->destination, destination, r->xy[4] + x, r->xy[5] + y, value & mask);
        }
    }
}
