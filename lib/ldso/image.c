/*
 * image.c - checked ELF32 segment admission and private image population
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "image.h"

static void zero(void *memory, uint32_t size)
{
    unsigned char *p = memory;

    while (size--)
        *p++ = 0;
}

static void copy(void *dst, const void *src, uint32_t size)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while (size--)
        *d++ = *s++;
}

static int extent(uint32_t start, uint32_t size, uint32_t limit)
{
    return start <= limit && size <= limit - start;
}

int dl_image_copy(const DL_IMAGE *p, const unsigned char *file, uint32_t length,
                    unsigned char *image, uint32_t capacity, uint32_t bias,
                    DL_OBJECT *object)
{
    DL_OBJECT o;
    uint32_t i;
    int rc;

    if (!object)
        return DL_BAD_ELF;
    zero(object, sizeof(*object));
    if (!p || !file || !image || capacity < p->size || !p->align ||
        bias % p->align || (p->type == 2 && bias) ||
        (uint64_t)p->base + bias + p->size > UINT32_MAX)
        return DL_BAD_ELF;
    for (i = 0; i < p->nload; i++)
        if (!extent(p->loads[i].offset, p->loads[i].filesz, length))
            return DL_BAD_ELF;
    zero(image, p->size);
    for (i = 0; i < p->nload; i++)
        copy(image + p->loads[i].address - p->base,
                file + p->loads[i].offset, p->loads[i].filesz);
    rc = dl_read_dynamic(&o, image, p->size, p->base, bias, p->dynamic, p->dynamic_size);
    if (rc)
        return rc;
    if (!dl_image_file_range(p, o.strtab, o.strsz, DL_PF_R) ||
        !dl_image_file_range(p, o.symtab, o.nchain * sizeof(DL_SYMBOL), DL_PF_R) ||
        !dl_image_file_range(p, o.hash, 8 + (o.nbucket + o.nchain) * 4, DL_PF_R) ||
        (o.relasz && !dl_image_file_range(p, o.rela, o.relasz, DL_PF_R)) ||
        (o.pltrelsz && !dl_image_file_range(p, o.jmprel, o.pltrelsz, DL_PF_R)))
        return DL_BAD_ELF;
    *object = o;
    return DL_OK;
}
