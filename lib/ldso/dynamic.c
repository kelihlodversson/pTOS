/*
 * dynamic.c - userspace ELF32 dependency and symbol metadata (#660)
 *
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "dynamic.h"

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_STRSZ   10
#define DT_SYMENT  11
#define DT_SONAME  14
#define DT_GNU_HASH 0x6ffffef5UL

/* Copy through bytes: neither on-disk offsets nor client buffers need be
 * aligned, and an ELF object is not a C array of uint32_t or DL_SYMBOL. */
static void copy_bytes(void *dst, const void *src, uint32_t bytes)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while (bytes--)
        *d++ = *s++;
}

static const unsigned char *image_at(const DL_OBJECT *o, uint32_t va,
                                    uint32_t bytes)
{
    uint32_t offset;

    if (va < o->link_base)
        return 0;
    offset = va - o->link_base;
    if (offset > o->image_size || bytes > o->image_size - offset)
        return 0;
    return o->image + offset;
}

static uint32_t read_word(const void *src)
{
    uint32_t word;

    copy_bytes(&word, src, sizeof(word));
    return word;
}

const char *dl_string(const DL_OBJECT *o, uint32_t offset)
{
    const unsigned char *s;
    uint32_t i;

    if (offset >= o->strsz)
        return 0;
    s = image_at(o, o->strtab, o->strsz);
    if (!s)
        return 0;
    for (i = offset; i < o->strsz; i++)
        if (!s[i])
            return (const char *)s + offset;
    return 0;
}

int dl_read_dynamic(DL_OBJECT *out, const unsigned char *image,
                    uint32_t image_size, uint32_t link_base, uint32_t bias,
                    uint32_t dyn_vaddr, uint32_t dyn_size)
{
    DL_OBJECT o = { 0 };
    DL_OBJECT empty = { 0 };
    const unsigned char *dyn;
    const unsigned char *hash;
    uint32_t pos, tag, value, seen = 0, bit, syment = 0, i;
    int terminated = 0, have_gnu_hash = 0;

    *out = empty;
    if (!image || !image_size || image_size - 1 > UINT32_MAX - link_base)
        return DL_BAD_ELF;
    o.image = image;
    o.image_size = image_size;
    o.link_base = link_base;
    o.bias = bias;
    o.dynamic = dyn_vaddr;
    o.dynamic_size = dyn_size;
    dyn = image_at(&o, dyn_vaddr, dyn_size);
    if (!dyn || dyn_size % 8)
        return DL_BAD_ELF;
    for (pos = 0; pos < dyn_size; pos += 8) {
        tag = read_word(dyn + pos);
        value = read_word(dyn + pos + 4);
        if (tag == DT_NULL) {
            terminated = 1;
            break;
        }
        bit = 0;
        switch (tag) {
        case DT_NEEDED:
            if (o.needed_count == DL_MAX_NEEDED)
                return DL_UNSUPPORTED;
            o.needed[o.needed_count++] = value;
            break;
        case DT_HASH: bit = 1; o.hash = value; break;
        case DT_STRTAB: bit = 2; o.strtab = value; break;
        case DT_SYMTAB: bit = 4; o.symtab = value; break;
        case DT_STRSZ: bit = 8; o.strsz = value; break;
        case DT_SYMENT: bit = 16; syment = value; break;
        case DT_SONAME: bit = 32; o.soname = value; o.has_soname = 1; break;
        case DT_GNU_HASH: have_gnu_hash = 1; break;
        case 7: bit = 1U << 6; o.rela = value; o.reloc_seen |= 1; break;
        case 8: bit = 1U << 7; o.relasz = value; o.reloc_seen |= 2; break;
        case 9: bit = 1U << 8; o.relaent = value; o.reloc_seen |= 4; break;
        case 23: bit = 1U << 9; o.jmprel = value; o.reloc_seen |= 8; break;
        case 2: bit = 1U << 10; o.pltrelsz = value; o.reloc_seen |= 16; break;
        case 20: bit = 1U << 11; o.pltrel = value; o.reloc_seen |= 32; break;
        case 17: bit = 1U << 12; o.rel = value; o.reloc_seen |= 64; break;
        case 18: bit = 1U << 13; o.relsz = value; o.reloc_seen |= 128; break;
        case 19: bit = 1U << 14; o.relent = value; o.reloc_seen |= 256; break;
        case 30: bit = 1U << 15; o.flags = value; break;
        case 0x6ffffffbUL: bit = 1U << 16; o.flags1 = value; break;
        case 22: o.features |= DL_FEATURE_TEXTREL; break;
        case 35: case 36: case 37: o.features |= DL_FEATURE_RELR; break;
        case 0x6ffffff0UL: case 0x6ffffffcUL: case 0x6ffffffdUL:
        case 0x6ffffffeUL: case 0x6fffffffUL:
            o.features |= DL_FEATURE_VERSION; break;
        case 16: o.features |= DL_FEATURE_SYMBOLIC; break;
        case 15: case 29: o.features |= DL_FEATURE_PATHS; break;
        }
        if (seen & bit)
            return DL_BAD_ELF;
        seen |= bit;
    }
    if (!terminated || (seen & 30) != 30 || syment != sizeof(DL_SYMBOL))
        return DL_BAD_ELF;
    if (!(seen & 1))
        return have_gnu_hash ? DL_UNSUPPORTED : DL_BAD_ELF;
    hash = image_at(&o, o.hash, 8);
    if (!hash || !image_at(&o, o.strtab, o.strsz) || !o.strsz)
        return DL_BAD_ELF;
    o.nbucket = read_word(hash);
    o.nchain = read_word(hash + 4);
    /* Bound both tables before multiplying or adding their sizes. */
    if (!o.nbucket || !o.nchain || o.nbucket > (UINT32_MAX - 8) / 4
        || o.nchain > (UINT32_MAX - 8 - o.nbucket * 4) / 4
        || !image_at(&o, o.hash, 8 + (o.nbucket + o.nchain) * 4)
        || o.nchain > UINT32_MAX / sizeof(DL_SYMBOL)
        || !image_at(&o, o.symtab, o.nchain * sizeof(DL_SYMBOL)))
        return DL_BAD_ELF;
    if (image_at(&o, o.strtab, 1)[0] || !dl_string(&o, o.strsz - 1))
        return DL_BAD_ELF;
    for (i = 0; i < o.needed_count; i++)
        if (!dl_string(&o, o.needed[i]) || !*dl_string(&o, o.needed[i]))
            return DL_BAD_ELF;
    if (o.has_soname && (!dl_string(&o, o.soname) || !*dl_string(&o, o.soname)))
        return DL_BAD_ELF;
    *out = o;
    return DL_OK;
}

static uint32_t sysv_hash(const char *name)
{
    uint32_t h = 0, g;

    while (*name) {
        h = (h << 4) + (unsigned char)*name++;
        g = h & 0xf0000000U;
        if (g)
            h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int dl_find_symbol(const DL_OBJECT *o, const char *name,
                    DL_SYMBOL *symbol, uint32_t *address)
{
    const unsigned char *hash, *symtab;
    const char *str;
    DL_SYMBOL sym;
    uint32_t index, steps, value;
    unsigned int bind, type, visibility;

    if (!o->image || !o->nbucket || !o->nchain)
        return DL_BAD_ELF;
    hash = image_at(o, o->hash, 8 + (o->nbucket + o->nchain) * 4);
    symtab = image_at(o, o->symtab, o->nchain * sizeof(DL_SYMBOL));
    if (!hash || !symtab)
        return DL_BAD_ELF;
    index = read_word(hash + 8 + (sysv_hash(name) % o->nbucket) * 4);
    /* Malformed hash chains may cycle: bounded work, never an infinite loop. */
    for (steps = 0; index && steps < o->nchain; steps++) {
        if (index >= o->nchain)
            return DL_BAD_ELF;
        copy_bytes(&sym, symtab + index * sizeof(sym), sizeof(sym));
        str = dl_string(o, sym.name);
        if (!str)
            return DL_BAD_ELF;
        bind = sym.info >> 4;
        type = sym.info & 15;
        visibility = sym.other & 3;
        if (equal(name, str) && sym.shndx && (bind == 1 || bind == 2)
            && (visibility == 0 || visibility == 3)) {
            if (type > 2 || (sym.shndx >= 0xff00 && sym.shndx != 0xfff1))
                return DL_UNSUPPORTED;
            value = sym.value;
            if (sym.shndx != 0xfff1) {
                if (!image_at(o, value, sym.size)
                    || value > UINT32_MAX - o->bias)
                    return DL_BAD_ELF;
                value += o->bias;
            }
            *symbol = sym;
            *address = value;
            return DL_OK;
        }
        index = read_word(hash + 8 + o->nbucket * 4 + index * 4);
    }
    return index ? DL_BAD_ELF : DL_NOT_FOUND;
}

int dl_resolve_symbol(const DL_OBJECT *o, uint32_t index,
                        const DL_OBJECT *const *scope, uint32_t count,
                        uint32_t *address)
{
    const unsigned char *table;
    const char *name;
    DL_SYMBOL sym, found;
    uint32_t i, value;
    unsigned int bind, visibility;
    int rc;

    if (count > DL_MAX_OBJECTS || (count && !scope) || index >= o->nchain)
        return DL_BAD_ELF;
    table = image_at(o, o->symtab, o->nchain * sizeof(sym));
    if (!table)
        return DL_BAD_ELF;
    copy_bytes(&sym, table + index * sizeof(sym), sizeof(sym));
    if (!index) {
        if (sym.name || sym.value || sym.size || sym.info || sym.other || sym.shndx)
            return DL_BAD_ELF;
        *address = 0;
        return DL_OK;
    }
    name = dl_string(o, sym.name);
    if (!name)
        return DL_BAD_ELF;
    bind = sym.info >> 4;
    visibility = sym.other & 3;
    if (bind > 2 || (sym.info & 15) > 2 ||
        (sym.shndx >= 0xff00 && sym.shndx != 0xfff1))
        return DL_UNSUPPORTED;
    if (sym.shndx && (!bind || visibility)) {
        value = sym.value;
        if (sym.shndx != 0xfff1) {
            if (!image_at(o, value, sym.size) ||
                value > UINT32_MAX - o->bias)
                return DL_BAD_ELF;
            value += o->bias;
        }
        *address = value;
        return DL_OK;
    }
    if (bind && !visibility) {
        for (i = 0; i < count; i++) {
            if (!scope[i])
                return DL_BAD_ELF;
            rc = dl_find_symbol(scope[i], name, &found, &value);
            if (rc == DL_OK) {
                *address = value;
                return DL_OK;
            }
            if (rc != DL_NOT_FOUND)
                return rc;
        }
    }
    if (bind == 2 && !sym.shndx) {
        *address = 0;
        return DL_OK;
    }
    return DL_UNRESOLVED;
}
