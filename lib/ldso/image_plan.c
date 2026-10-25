/*
 * image_plan.c - checked ELF32 segment admission, without mapping or binding
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "image.h"

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_PHDR 6
#define PT_TLS 7
#define PT_GNU_STACK 0x6474e551UL
#define PT_GNU_RELRO 0x6474e552UL
#define PT_PTOS_RELOC 0x60000001UL
#define PT_PTOS_IMPORTS 0x60000002UL

static void zero(void *memory, uint32_t size)
{
    unsigned char *p = memory;

    while (size--)
        *p++ = 0;
}

static uint32_t number(const unsigned char *p, uint32_t bytes, uint32_t endian)
{
    uint32_t v = 0, i;

    for (i = 0; i < bytes; i++)
        v = (v << 8) | p[endian == 1 ? bytes - 1 - i : i];
    return v;
}

static int extent(uint32_t start, uint32_t size, uint32_t limit)
{
    return start <= limit && size <= limit - start;
}

static int contains(uint32_t base, uint32_t span, uint32_t start, uint32_t size)
{
    return start >= base && extent(start - base, size, span);
}

static int overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    return n && m && (uint64_t)a < (uint64_t)b + m &&
                        (uint64_t)b < (uint64_t)a + n;
}

static int covered(const DL_IMAGE *p, uint32_t address, uint32_t size,
                    uint32_t flags, int file_backed)
{
    uint32_t i;

    if (!size)
        return 0;
    for (i = 0; i < p->nload; i++) {
        const DL_LOAD *l = &p->loads[i];

        if ((l->flags & flags) == flags &&
            contains(l->address, file_backed ? l->filesz : l->memsz, address, size))
            return 1;
    }
    return 0;
}

int dl_image_range(const DL_IMAGE *p, uint32_t address, uint32_t size,
                    uint32_t flags)
{
    return covered(p, address, size, flags, 0);
}

int dl_image_read(DL_IMAGE *out, const unsigned char *file, uint32_t length,
                    uint32_t role, const DL_IMAGE_ABI *abi)
{
    DL_IMAGE p;
    uint32_t endian, page, phoff, phnum, i, j, end = 0, dynoff = 0;
    uint32_t relro_va = 0, relro_size = 0, relro_off = 0, relro_fs = 0, phdr_va = 0;
    int have_dynamic = 0, have_interp = 0, have_relro = 0, have_phdr = 0;

    if (!out)
        return DL_BAD_ELF;
    zero(out, sizeof(*out));
    zero(&p, sizeof(p));
    if (!abi || !file || length < 52 || role < DL_IMAGE_DSO ||
        role > DL_IMAGE_EXECUTABLE || (abi->endian != 1 && abi->endian != 2))
        return DL_BAD_ELF;
    endian = abi->endian;
    page = abi->page_size;
    if (!page || (page & (page - 1)) || !abi->max_span ||
        file[0] != 0x7f || file[1] != 'E' || file[2] != 'L' || file[3] != 'F' ||
        file[4] != 1 || file[5] != endian || file[6] != 1 ||
        number(file + 18, 2, endian) != abi->machine ||
        number(file + 20, 4, endian) != 1 ||
        (number(file + 36, 4, endian) & ~abi->eflags_allowed) ||
        (number(file + 36, 4, endian) & abi->eflags_required) != abi->eflags_required ||
        number(file + 40, 2, endian) != 52 || number(file + 42, 2, endian) != 32)
        return DL_BAD_ELF;
    p.type = number(file + 16, 2, endian);
    if ((p.type != 3 && (role != DL_IMAGE_EXECUTABLE || p.type != 2)))
        return DL_BAD_ELF;
    p.entry = number(file + 24, 4, endian);
    phoff = number(file + 28, 4, endian);
    phnum = number(file + 44, 2, endian);
    if (!phnum || phnum > DL_MAX_PHDRS || !extent(phoff, phnum * 32, length))
        return DL_BAD_ELF;
    p.phnum = phnum;
    p.base = UINT32_MAX;
    p.align = page;
    for (i = 0; i < phnum; i++) {
        const unsigned char *ph = file + phoff + i * 32;
        uint32_t type = number(ph, 4, endian), off = number(ph + 4, 4, endian);
        uint32_t va = number(ph + 8, 4, endian), fs = number(ph + 16, 4, endian);
        uint32_t ms = number(ph + 20, 4, endian), flags = number(ph + 24, 4, endian);
        uint32_t align = number(ph + 28, 4, endian);

        if (type == PT_TLS || type == PT_PTOS_RELOC || type == PT_PTOS_IMPORTS ||
            (type == PT_GNU_STACK && (flags & DL_PF_X)))
            return DL_UNSUPPORTED;
        if (type == PT_INTERP) {
            if (have_interp++ || role != DL_IMAGE_EXECUTABLE || fs < 2 || fs != ms ||
                fs > DL_MAX_INTERP || !extent(off, fs, length) || file[off] != '/' ||
                file[off + fs - 1])
                return DL_BAD_ELF;
            for (j = 0; j < fs - 1; j++)
                if (!file[off + j])
                    return DL_BAD_ELF;
            p.interp_offset = off;
            p.interp_size = fs;
        }
        if (type == PT_DYNAMIC) {
            if (have_dynamic++ || !fs || fs % 8 || fs != ms ||
                !extent(off, fs, length))
                return DL_BAD_ELF;
            p.dynamic = va;
            p.dynamic_size = fs;
            dynoff = off;
        }
        if (type == PT_PHDR) {
            if (have_phdr++ || off != phoff || fs != phnum * 32 || ms != fs)
                return DL_BAD_ELF;
            phdr_va = va;
        }
        if (type == PT_GNU_RELRO) {
            if (have_relro++ || !ms || fs > ms || !extent(off, fs, length) ||
                !extent(va, ms, UINT32_MAX))
                return DL_BAD_ELF;
            relro_va = va;
            relro_size = ms;
            relro_off = off;
            relro_fs = fs;
        }
        if (type != PT_LOAD)
            continue;
        if (fs > ms || !extent(off, fs, length) || !extent(va, ms, UINT32_MAX) ||
            (flags & ~7U) || !(flags & DL_PF_R) || (flags & 3) == 3 ||
            (align > 1 && ((align & (align - 1)) || (va % align != off % align))) ||
            (va % page != off % page))
            return DL_BAD_ELF;
        if (!ms)
            continue;
        if (p.nload == DL_MAX_LOADS)
            return DL_NO_SPACE;
        if (va < p.base)
            p.base = va;
        if (va + ms > end)
            end = va + ms;
        if (align > p.align)
            p.align = align;
        p.loads[p.nload].offset = off;
        p.loads[p.nload].address = va;
        p.loads[p.nload].filesz = fs;
        p.loads[p.nload].memsz = ms;
        p.loads[p.nload].flags = flags;
        p.loads[p.nload++].align = align;
    }
    if (!p.nload || !have_dynamic || (role == DL_IMAGE_EXECUTABLE && !have_interp) ||
        end > UINT32_MAX - (page - 1))
        return DL_BAD_ELF;
    p.base &= ~(page - 1);
    end = (end + page - 1) & ~(page - 1);
    p.size = end - p.base;
    if (p.size > abi->max_span)
        return DL_NO_SPACE;
    if (role != DL_IMAGE_DSO && !covered(&p, p.entry, 1, DL_PF_X, 1))
        return DL_BAD_ELF;
    for (i = 0; i < p.nload; i++) {
        const DL_LOAD *a = &p.loads[i];
        uint32_t first = a->address & ~(page - 1);
        uint32_t last = (a->address + a->memsz + page - 1) & ~(page - 1);

        for (j = 0; j < i; j++) {
            const DL_LOAD *b = &p.loads[j];
            uint32_t bfirst = b->address & ~(page - 1);
            uint32_t blast = (b->address + b->memsz + page - 1) & ~(page - 1);

            if (overlap(a->address, a->memsz, b->address, b->memsz) ||
                (a->flags != b->flags && overlap(first, last - first, bfirst, blast - bfirst)))
                return DL_BAD_ELF;
        }
        if (contains(a->offset, a->filesz, phoff, phnum * 32))
            p.phdr = a->address + phoff - a->offset;
    }
    if ((have_phdr && (!p.phdr || p.phdr != phdr_va)) ||
        (role == DL_IMAGE_EXECUTABLE && !p.phdr) ||
        !covered(&p, p.dynamic, p.dynamic_size, DL_PF_R, 1))
        return DL_BAD_ELF;
    for (i = 0; i < p.nload; i++) {
        const DL_LOAD *a = &p.loads[i];

        if (contains(a->address, a->filesz, p.dynamic, p.dynamic_size) &&
            a->offset + p.dynamic - a->address == dynoff)
            break;
    }
    if (i == p.nload)
        return DL_BAD_ELF;
    if (have_relro) {
        uint32_t first = relro_va & ~(page - 1);
        uint32_t last = (relro_va + relro_size) & ~(page - 1);

        if (!covered(&p, relro_va, relro_size, DL_PF_R | DL_PF_W, 0))
            return DL_BAD_ELF;
        if (relro_fs) {
            for (i = 0; i < p.nload; i++) {
                const DL_LOAD *a = &p.loads[i];

                if (contains(a->address, a->filesz, relro_va, relro_fs) &&
                    a->offset + relro_va - a->address == relro_off)
                    break;
            }
            if (i == p.nload)
                return DL_BAD_ELF;
        }
        p.relro = first;
        p.relro_size = last - first;
    }
    *out = p;
    return DL_OK;
}

int dl_image_file_range(const DL_IMAGE *p, uint32_t address, uint32_t size, uint32_t flags)
{
    return covered(p, address, size, flags, 1);
}
