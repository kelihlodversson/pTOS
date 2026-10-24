/*
 * relocate.c - userspace ELF32 RELA planning and eager binding (#660)
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "relocate.h"

typedef struct {
    uint32_t offset, info;
    int32_t addend;
} DL_RELA;

static void copy_bytes(void *dst, const void *src, uint32_t bytes)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while (bytes--)
        *d++ = *s++;
}

static const unsigned char *at(const DL_OBJECT *o, uint32_t va, uint32_t size)
{
    if (va < o->link_base || va - o->link_base > o->image_size ||
        size > o->image_size - (va - o->link_base))
        return 0;
    return o->image + (va - o->link_base);
}

static int overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    return n && m && (uint64_t)a < (uint64_t)b + m &&
           (uint64_t)b < (uint64_t)a + n;
}

static int metadata(const DL_OBJECT *o, uint32_t va, uint32_t size)
{
    return overlap(va, size, o->dynamic, o->dynamic_size) ||
           overlap(va, size, o->strtab, o->strsz) ||
           overlap(va, size, o->symtab, o->nchain * sizeof(DL_SYMBOL)) ||
           overlap(va, size, o->hash, 8 + (o->nbucket + o->nchain) * 4) ||
           overlap(va, size, o->rela, o->relasz) ||
           overlap(va, size, o->jmprel, o->pltrelsz);
}

static int host_overlap(const unsigned char *image, uint32_t size,
                         const DL_RELOC_WRITE *work, uint32_t bytes)
{
    uintptr_t a = (uintptr_t)image, b = (uintptr_t)work;

    return size && bytes && (a <= b ? b - a < size : a - b < bytes);
}

int dl_relocate(const DL_OBJECT *o, unsigned char *image,
                const DL_OBJECT *const *scope, uint32_t scope_count,
                const DL_WRITE_RANGE *ranges, uint32_t range_count,
                DL_RELOC_WRITE *work, uint32_t capacity, uint32_t *written,
                const DL_RELOC_BACKEND *backend)
{
    uint32_t i, j, table, start, size, pos, used = 0, symbol, place;
    uint32_t total;
    uint64_t value;
    DL_RELA rel;
    DL_RELOC_KIND kind;
    int rc, writable;

    *written = 0;
    if (!o || !image || image != o->image || !backend ||
        !backend->kind || !backend->value || scope_count > DL_MAX_OBJECTS ||
        (scope_count && !scope) || (range_count && !ranges))
        return DL_BAD_ELF;
    if (range_count > DL_MAX_WRITE_RANGES)
        return DL_UNSUPPORTED;
    if (o->features || (o->flags & ~8U) || (o->flags1 & ~0x08000001U) ||
        (o->reloc_seen & (64 | 128 | 256)))
        return DL_UNSUPPORTED;
    /* RELA triplets and PLT metadata must be complete. RELAENT alone is
     * allowed for PLT-only output, whose encoding DT_PLTREL identifies. */
    if (((o->reloc_seen & 3) && (o->reloc_seen & 7) != 7) ||
        ((o->reloc_seen & 4) && o->relaent != sizeof(DL_RELA)) ||
        ((o->reloc_seen & 56) && (o->reloc_seen & 56) != 56))
        return DL_BAD_ELF;
    if ((o->reloc_seen & 56) && o->pltrel != 7)  /* DT_RELA */
        return DL_UNSUPPORTED;
    if (o->relasz % sizeof(DL_RELA) || o->pltrelsz % sizeof(DL_RELA) ||
        (o->relasz && !at(o, o->rela, o->relasz)) ||
        (o->pltrelsz && !at(o, o->jmprel, o->pltrelsz)))
        return DL_BAD_ELF;
    total = o->relasz / sizeof(DL_RELA);
    if (overlap(o->rela, o->relasz, o->jmprel, o->pltrelsz)) {
        if (o->jmprel < o->rela || o->pltrelsz > o->relasz ||
            o->jmprel - o->rela > o->relasz - o->pltrelsz ||
            (o->jmprel - o->rela) % sizeof(DL_RELA))
            return DL_BAD_ELF;
    } else {
        total += o->pltrelsz / sizeof(DL_RELA);
    }
    if (total > DL_MAX_RELOCATIONS)
        return DL_UNSUPPORTED;
    if (capacity > DL_MAX_RELOCATIONS)
        capacity = DL_MAX_RELOCATIONS;
    if ((capacity && !work) ||
        host_overlap(image, o->image_size, work, capacity * sizeof(*work)))
        return DL_BAD_ELF;
    for (i = 0; i < scope_count; i++)
        if (!scope[i] || host_overlap(scope[i]->image, scope[i]->image_size,
                                       work, capacity * sizeof(*work)))
            return DL_BAD_ELF;
    for (i = 0; i < range_count; i++)
        if (!ranges[i].size || !at(o, ranges[i].address, ranges[i].size))
            return DL_BAD_ELF;

    for (table = 0; table < 2; table++) {
        start = table ? o->jmprel : o->rela;
        size = table ? o->pltrelsz : o->relasz;
        for (pos = 0; pos < size; pos += sizeof(DL_RELA)) {
            if (!table && overlap(start + pos, sizeof(DL_RELA),
                                    o->jmprel, o->pltrelsz))
                continue;              /* PLT subset handled once below */
            copy_bytes(&rel, at(o, start + pos, sizeof(rel)), sizeof(rel));
            rc = backend->kind(rel.info & 255, &kind);
            if (rc)
                return rc;
            if (!kind.width)
                continue;              /* R_*_NONE */
            if (kind.width != 4 && kind.width != 8)
                return DL_UNSUPPORTED;
            if (table && !(kind.flags & DL_RELOC_PLT))
                return DL_UNSUPPORTED;
            if (!at(o, rel.offset, kind.width) ||
                metadata(o, rel.offset, kind.width) ||
                (uint64_t)rel.offset + o->bias + kind.width - 1 > UINT32_MAX)
                return DL_BAD_ELF;
            writable = 0;
            for (i = 0; i < range_count; i++)
                if (rel.offset >= ranges[i].address &&
                    rel.offset - ranges[i].address <= ranges[i].size &&
                    kind.width <= ranges[i].size - (rel.offset - ranges[i].address))
                    writable = 1;
            if (!writable)
                return DL_BAD_ELF;
            if ((kind.flags & DL_RELOC_RELATIVE) && (rel.info >> 8))
                return DL_BAD_ELF;
            symbol = 0;
            if (kind.flags & DL_RELOC_SYMBOL) {
                rc = dl_resolve_symbol(o, rel.info >> 8, scope, scope_count, &symbol);
                if (rc)
                    return rc;
            }
            place = rel.offset + o->bias;
            rc = backend->value(rel.info & 255, o->bias, symbol,
                                 rel.addend, place, &value);
            if (rc)
                return rc;
            if (used == capacity)
                return DL_NO_SPACE;
            for (j = 0; j < used; j++)
                if (overlap(rel.offset - o->link_base, kind.width,
                             work[j].offset, work[j].width))
                    return DL_BAD_ELF;
            work[used].offset = rel.offset - o->link_base;
            work[used].width = kind.width;
            work[used++].value = value;
        }
    }
    /* No operation above has written an object byte. Source tables and all
     * destinations are validated, and the plan is separate from the images. */
    for (i = 0; i < used; i++) {
        if (work[i].width == 4) {
            uint32_t word = (uint32_t)work[i].value;

            copy_bytes(image + work[i].offset, &word, 4);
        } else {
            copy_bytes(image + work[i].offset, &work[i].value, 8);
        }
    }
    *written = used;
    return DL_OK;
}
