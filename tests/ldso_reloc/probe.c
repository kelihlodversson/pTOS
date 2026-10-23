/*
 * Execute independently built PIC DSOs in a real ring-3 x32 process (#660).
 * This fixture loader is a test harness, not PT_INTERP implementation.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "../../lib/ldso/relocate.h"
#include "../../lib/ldso/graph.h"
#include "../../include/mman.h"

extern const unsigned char dl_test_leaf_start[], dl_test_leaf_end[];
extern const unsigned char dl_test_library_start[], dl_test_library_end[];

typedef struct {
    DL_OBJECT object;
    uint32_t address, size, nseg, nwritable;
    struct { uint32_t va, size, flags; } seg[16];
    DL_WRITE_RANGE writable[16];
} SAMPLE;

static int64_t call(uint32_t fn, uint64_t a, uint64_t b, uint64_t c,
                     uint64_t d, uint64_t e, uint64_t f)
{
    register uint64_t ax __asm__("rax") = ((uint64_t)1 << 32) | fn;
    register uint64_t di __asm__("rdi") = a;
    register uint64_t si __asm__("rsi") = b;
    register uint64_t dx __asm__("rdx") = c;
    register uint64_t r10 __asm__("r10") = d;
    register uint64_t r8 __asm__("r8") = e;
    register uint64_t r9 __asm__("r9") = f;

    __asm__ volatile ("syscall" : "+a"(ax)
                      : "r"(di), "r"(si), "r"(dx), "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return (int64_t)ax;
}

static void bytes(void *to, const void *from, uint32_t n)
{
    unsigned char *d = to;
    const unsigned char *s = from;

    while (n--)
        *d++ = *s++;
}

static uint32_t word(const unsigned char *p)
{
    uint32_t v;

    bytes(&v, p, 4);
    return v;
}

/* The samples are small native-endian ELF32 ET_DYN objects with link base 0.
 * Check all file/memory extents before copying into new anonymous pages. */
static int load(SAMPLE *s, const unsigned char *file, uint32_t length)
{
    uint32_t phoff, count, i, end = 0, base = UINT32_MAX;
    uint32_t dynamic = 0, dynsize = 0;
    int64_t mapped;

    if (length < 52 || word(file) != 0x464c457fUL || file[4] != 1 ||
        file[5] != 1 || file[16] != 3 || file[17] || file[18] != 62 || file[19] ||
        file[42] != 32 || file[43] || file[45])
        return 0;
    count = file[44];
    phoff = word(file + 28);
    if (!count || count > 16 || phoff > length || count * 32 > length - phoff)
        return 0;
    s->nseg = s->nwritable = 0;
    for (i = 0; i < count; i++) {
        const unsigned char *ph = file + phoff + i * 32;
        uint32_t type = word(ph), off = word(ph + 4), va = word(ph + 8);
        uint32_t filesz = word(ph + 16), memsz = word(ph + 20), flags = word(ph + 24);
        uint32_t align = word(ph + 28);

        if (type == 3)                  /* no interpreter in a DSO */
            return 0;
        if (type == 2) {
            if (dynsize || !filesz || off > length || filesz > length - off)
                return 0;
            dynamic = va;
            dynsize = filesz;
        }
        if (type != 1 || !memsz)
            continue;
        if (filesz > memsz || off > length || filesz > length - off ||
            va > 0x100000 || memsz > 0x100000 - va || (flags & 3) == 3 ||
            (align > 1 && ((align & (align - 1)) || (va - off) % align)))
            return 0;
        if (va < base)
            base = va;
        if (va + memsz > end)
            end = va + memsz;
        s->seg[s->nseg].va = va;
        s->seg[s->nseg].size = memsz;
        s->seg[s->nseg++].flags = flags;
        if (flags & 2) {
            s->writable[s->nwritable].address = va;
            s->writable[s->nwritable++].size = memsz;
        }
    }
    if (base || !dynsize || !end)
        return 0;
    s->size = (end + 4095) & ~4095U;
    mapped = call(GEMDOS_MMAP, 0, s->size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, UINT64_MAX, 0);
    if (mapped <= 0)
        return 0;
    s->address = (uint32_t)mapped;
    for (i = 0; i < count; i++) {
        const unsigned char *ph = file + phoff + i * 32;

        if (word(ph) == 1 && word(ph + 20))
            bytes((void *)(uintptr_t)(s->address + word(ph + 8)),
                   file + word(ph + 4), word(ph + 16));
    }
    return dl_read_dynamic(&s->object, (const unsigned char *)(uintptr_t)s->address,
                            s->size, 0, s->address, dynamic, dynsize) == DL_OK;
}

static int seal(const SAMPLE *s)
{
    uint32_t i;

    if (call(GEMDOS_MPROT, s->address, s->size, PROT_NONE, 0, 0, 0))
        return 0;
    for (i = 0; i < s->nseg; i++) {
        uint32_t first = s->seg[i].va & ~4095U;
        uint32_t end = (s->seg[i].va + s->seg[i].size + 4095) & ~4095U;
        uint32_t prot = PROT_READ;

        if (s->seg[i].flags & 2)
            prot |= PROT_WRITE;
        if (s->seg[i].flags & 1)
            prot |= PROT_EXEC;
        if (call(GEMDOS_MPROT, s->address + first, end - first, prot, 0, 0, 0))
            return 0;
    }
    return 1;
}

/* Large graph storage belongs in private .bss, not on the user stack. */
static DL_GRAPH graph;

typedef struct {
    SAMPLE *leaf;
    int acquisitions, release_bad;
} FIXTURE_CONTEXT;

static int acquire(void *context, const char *name, DL_MAPPING *mapping)
{
    FIXTURE_CONTEXT *ctx = context;
    const char *expected = "libleaf.so";
    uint32_t i = 0;

    while (name[i] && name[i] == expected[i])
        i++;
    if (name[i] != expected[i])
        return DL_NOT_FOUND;
    ctx->acquisitions++;
    if (!load(ctx->leaf, dl_test_leaf_start,
                (uint32_t)(dl_test_leaf_end - dl_test_leaf_start))) {
        if (ctx->leaf->address) {
            call(GEMDOS_MUNMAP, ctx->leaf->address, ctx->leaf->size, 0, 0, 0, 0);
            ctx->leaf->address = 0;
        }
        return DL_BAD_ELF;
    }
    mapping->object = ctx->leaf->object;
    mapping->identity = 2;
    mapping->cookie = ctx->leaf;
    return DL_OK;
}

static void release(void *context, const DL_MAPPING *mapping)
{
    FIXTURE_CONTEXT *ctx = context;
    SAMPLE *sample = mapping->cookie;

    if (call(GEMDOS_MUNMAP, sample->address, sample->size, 0, 0, 0, 0))
        ctx->release_bad = 1;
    sample->address = 0;
}

static const DL_GRAPH_OPS graph_ops = { acquire, release };

int x32_ldso_probe(void);

int x32_ldso_probe(void)
{
    int cycle, rc = 0;

    for (cycle = 0; cycle < 2; cycle++) {
        SAMPLE leaf = { 0 }, library = { 0 };
        FIXTURE_CONTEXT context = { &leaf, 0, 0 };
        DL_MAPPING root;
        DL_RELOC_WRITE work[32];
        DL_SYMBOL symbol;
        uint32_t written, answer, value, scratch, i, init = 0, initsz = 0;
        int (*function)(void);

        if (!load(&library, dl_test_library_start,
                   (uint32_t)(dl_test_library_end - dl_test_library_start))) {
            rc = 2; goto done;
        }
        root.object = library.object;
        root.identity = 1;
        root.cookie = &library;
        if (dl_graph_open(&graph, &root, &graph_ops, &context) ||
            graph.count != 2 || graph.order_count != 2 ||
            graph.order[0] != 1 || graph.order[1] != 0 || context.acquisitions != 1) {
            rc = 1; goto done;
        }
        if (dl_relocate(&leaf.object, (unsigned char *)(uintptr_t)leaf.address,
                         graph.scope, graph.count, leaf.writable, leaf.nwritable, work, 32, &written,
                         &dl_x32_reloc_backend)) {
            rc = 3; goto done;
        }
        if (dl_relocate(&library.object, (unsigned char *)(uintptr_t)library.address,
                         graph.scope, graph.count, library.writable, library.nwritable, work, 32, &written,
                         &dl_x32_reloc_backend)) {
            rc = 4; goto done;
        }
        if (!seal(&leaf) || !seal(&library)) {
            rc = 5; goto done;
        }
        if (dl_find_symbol(&library.object, "answer", &symbol, &answer) ||
            dl_find_symbol(&library.object, "value", &symbol, &value) ||
            dl_find_symbol(&library.object, "scratch", &symbol, &scratch)) {
            rc = 6; goto done;
        }
        function = (int (*)(void))(uintptr_t)answer;
        if (*(uint32_t *)(uintptr_t)value != 5 || *(uint32_t *)(uintptr_t)scratch ||
            function() != 12) {           /* PLT -> leaf(), GOT -> value/bss */
            rc = 7; goto done;
        }
        for (i = 0; i < library.object.dynamic_size; i += 8) {
            const unsigned char *d = library.object.image + library.object.dynamic + i;
            uint32_t tag = word(d);

            if (!tag)
                break;
            if (tag == 25)
                init = word(d + 4);
            if (tag == 27)
                initsz = word(d + 4);
        }
        if (initsz != 4 || init > library.size - 4) {
            rc = 8; goto done;
        }
        ((void (*)(void))(uintptr_t)word(library.object.image + init))();
        if (function() != 16 || *(uint32_t *)(uintptr_t)value != 9) {
            rc = 9; goto done;
        }
        *(uint32_t *)(uintptr_t)value = 20;
        *(uint32_t *)(uintptr_t)scratch = 3;
        if (function() != 30)
            rc = 10;
done:
        dl_graph_close(&graph, &graph_ops, &context);
        if (context.release_bad)
            rc = 12;
        if (library.address && call(GEMDOS_MUNMAP, library.address, library.size, 0, 0, 0, 0))
            rc = 11;
        if (leaf.address && call(GEMDOS_MUNMAP, leaf.address, leaf.size, 0, 0, 0, 0))
            rc = 12;
        if (rc)
            return rc;
    }
    return 0;
}
