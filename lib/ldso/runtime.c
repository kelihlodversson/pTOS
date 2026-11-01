/*
 * runtime.c - initial freestanding userspace ELF interpreter (#660)
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "runtime.h"
#include "../../include/mman.h"

#define FILE_LIMIT 0x00800000U
#define MAX_HOOKS 4096U

typedef struct {
    uint32_t init, fini, init_array, init_size, fini_array, fini_size, pre_array, pre_size;
} HOOKS;
typedef struct {
    DL_IMAGE plan;
    DL_OBJECT object;
    DL_WRITE_RANGE writable[DL_MAX_LOADS];
    uint32_t nwritable, reservation, reservation_size;
    uint64_t identity;
    char canonical[13];
    HOOKS hooks;
} OBJECT;

static OBJECT objects[DL_MAX_OBJECTS], self;
static DL_GRAPH graph;
static DL_RELOC_WRITE work[DL_MAX_RELOCATIONS];
static uint32_t used_objects, argc, bootdrive, initialized, finalized;
static const uint32_t *argv, *envp;
static const char *error_name;
static char error_buffer[64];
extern const uint64_t dl_self_pointer64;

static uint32_t word(const unsigned char *p)
{
    uint32_t value, i;
    for (i = 0; i < 4; i++) ((unsigned char *)&value)[i] = p[i];
    return value;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void message(const char *text)
{
    dl_syscall(9, (uintptr_t)text, 0, 0, 0, 0, 0);
}
static uint32_t fail(void)
{
    message("\r\nLD.SO: cannot load ");
    message(error_name ? error_name : "ELF image");
    message("\r\n");
    return 0;
}
static uint32_t map(uint32_t size)
{
    int64_t at = dl_syscall(GEMDOS_MMAP, 0, size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, UINT64_MAX, 0);
    return at > 0 ? (uint32_t)at : 0;
}
static uint32_t map_file(uint32_t at, uint32_t size, uint32_t prot, int handle, uint32_t offset)
{
    int64_t result = dl_syscall(GEMDOS_MMAP, at, size, prot,
        MAP_PRIVATE | (at ? MAP_FIXED_NOREPLACE : 0), handle, offset);
    return result > 0 ? (uint32_t)result : 0;
}
static void unmap(uint32_t address, uint32_t size)
{
    if (address) dl_syscall(GEMDOS_MUNMAP, address, size, 0, 0, 0, 0);
}
static int protect(uint32_t address, uint32_t size, uint32_t prot)
{
    return !size || dl_syscall(GEMDOS_MPROT, address, size, prot, 0, 0, 0) == 0;
}
static void writable(OBJECT *o)
{
    uint32_t i;
    o->nwritable = 0;
    for (i = 0; i < o->plan.nload; i++)
        if (o->plan.loads[i].flags & DL_PF_W) {
            o->writable[o->nwritable].address = o->plan.loads[i].address;
            o->writable[o->nwritable++].size = o->plan.loads[i].memsz;
        }
}

/* Kernel startup headers are immutable and already admitted. Reconstruct
 * views from PT_LOAD and the validated load bias, without reopening files
 * or requiring the optional PT_PHDR program header. */
static int loaded(OBJECT *o, const unsigned char *ph, uint32_t count, uint32_t bias)
{
    uint32_t i, low = UINT32_MAX, high = 0;
    o->plan.align = 4096;
    if (!count || count > DL_MAX_PHDRS) return 0;
    for (i = 0; i < count; i++, ph += 32) {
        uint32_t type = word(ph), va = word(ph + 8), fs = word(ph + 16), ms = word(ph + 20);
        if (type == 2) { o->plan.dynamic = va; o->plan.dynamic_size = fs; }
        if (type == 0x6474e552UL) {
            if ((uint64_t)va + ms > UINT32_MAX) return 0;
            o->plan.relro = va & ~4095U;
            o->plan.relro_size = ((va + ms) & ~4095U) - o->plan.relro;
        }
        if (type == 1 && ms) {
            DL_LOAD *l;
            if (o->plan.nload == DL_MAX_LOADS || fs > ms ||
                (uint64_t)va + ms + bias >= UINT32_MAX) return 0;
            l = &o->plan.loads[o->plan.nload++];
            l->offset = word(ph + 4); l->address = va; l->filesz = fs;
            l->memsz = ms; l->flags = word(ph + 24); l->align = word(ph + 28);
            if (va < low) low = va;
            if (va + ms > high) high = va + ms;
        }
    }
    if (!o->plan.nload || !o->plan.dynamic_size || high > UINT32_MAX - 4095) return 0;
    o->plan.base = low & ~4095U;
    o->plan.size = ((high + 4095) & ~4095U) - o->plan.base;
    writable(o);
    return dl_image_view(&o->plan, (const unsigned char *)(uintptr_t)(o->plan.base + bias),
                          bias, &o->object) == DL_OK;
}

/* Strict 8.3 names give a unique entry in the one trusted FAT directory.
 * Requiring an exact SONAME rejects case-folded aliases. Each process uses
 * its first retained mapping and never reopens that name during discovery.
 * Hash collisions between different canonical names are rejected below. */
static uint64_t canonical(const char *name, char *out)
{
    uint32_t i, base = 0, extension = 0, dot = 0;
    uint64_t key = 14695981039346656037ULL;
    for (i = 0; name[i]; i++) {
        unsigned char c = name[i];
        if (i >= 12) return 0;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c == '.') { if (dot || !base) return 0; dot = 1; }
        else {
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
            if (dot) { if (++extension > 3) return 0; }
            else if (++base > 8) return 0;
        }
        out[i] = c;
        key = (key ^ c) * 1099511628211ULL;
    }
    out[i] = 0;
    return dot && extension && key > 1 ? key : 0;
}

/* Map each unambiguous segment page from the retained file. Shared boundary
 * pages or non-page-congruent layouts use anonymous copies instead. Extra file
 * bytes outside a segment's logical extent may occupy its rounded file page,
 * as in ordinary ELF mappings; only actual BSS requires zeroing. */
static int populate(OBJECT *o, uint32_t file, uint32_t address, int handle)
{
    uint32_t i, j, page, bias = address - o->plan.base;
    for (i = 0; i < o->plan.nload; i++) {
        DL_LOAD *l = &o->plan.loads[i];
        uint32_t file_end = l->address + l->filesz;
        for (page = l->address & ~4095U; l->filesz && page < file_end; page += 4096) {
            uint32_t begin = page > l->address ? page : l->address;
            uint32_t end = page + 4096 < file_end ? page + 4096 : file_end;
            uint32_t at, offset = l->offset + begin - l->address;
            int direct = (l->offset & 4095) == (l->address & 4095) &&
                         l->offset >= (l->address & 4095);
            for (j = 0; direct && j < o->plan.nload; j++) {
                DL_LOAD *other = &o->plan.loads[j];
                if (j != i && other->memsz && page < other->address + other->memsz &&
                    other->address < page + 4096) direct = 0;
            }
            if (direct) {
                uint32_t protection = PROT_READ;
                /* Partial-page BSS must be zeroed through private COW even
                 * for an ultimately read-only or executable segment. seal()
                 * installs the final permissions after population/binding. */
                if ((l->flags & DL_PF_W) ||
                    (l->memsz > l->filesz && file_end < page + 4096))
                    protection |= PROT_WRITE;
                offset -= begin - page;
                if (dl_syscall(GEMDOS_MUNMAP, bias + page, 4096, 0, 0, 0, 0) ||
                    map_file(bias + page, 4096, protection,
                             handle, offset) != bias + page) return 0;
            } else {
                const unsigned char *from = (const unsigned char *)(uintptr_t)(file + offset);
                unsigned char *to = (unsigned char *)(uintptr_t)(bias + begin);
                for (at = begin; at < end; at++) *to++ = *from++;
            }
        }
        if (l->memsz > l->filesz && (file_end & 4095)) {
            uint32_t end = (file_end + 4095) & ~4095U;
            if (end > l->address + l->memsz) end = l->address + l->memsz;
            for (page = file_end; page < end; page++)
                *(unsigned char *)(uintptr_t)(bias + page) = 0;
        }
    }
    return dl_image_view(&o->plan, (const unsigned char *)(uintptr_t)address, bias, &o->object) == DL_OK;
}
static int acquire(void *context, const char *name, DL_MAPPING *mapping)
{
    OBJECT *o;
    char path[40], canon[13] = { 0 };
    uint64_t identity = canonical(name, canon);
    uint32_t i, j, file = 0, size = 0, address, reserve;
    int64_t handle = -1, length;
    int rc = DL_BAD_ELF;
    (void)context;
    for (i = 0; i < sizeof(error_buffer) - 1 && name[i]; i++) error_buffer[i] = name[i];
    error_buffer[i] = 0;
    error_name = error_buffer;
    if (!identity || used_objects == DL_MAX_OBJECTS) return DL_UNSUPPORTED;
    for (i = 1; i < used_objects; i++)
        if (objects[i].identity == identity && !equal(objects[i].canonical, canon))
            return DL_BAD_ELF;
    path[0] = 'A' + bootdrive; path[1] = ':';
    for (i = 0; "\\GEMSYS\\LIB\\X32\\"[i]; i++) path[i + 2] = "\\GEMSYS\\LIB\\X32\\"[i];
    for (j = 0; ; j++) { path[i + 2 + j] = name[j]; if (!name[j]) break; }
    handle = dl_syscall(0x3d, (uintptr_t)path, 0, 0, 0, 0, 0);
    if (handle < 0) return DL_NOT_FOUND;
    length = dl_syscall(0x42, 0, handle, 2, 0, 0, 0);
    if (length < 52 || length > FILE_LIMIT) goto done;
    size = length; file = map_file(0, size, PROT_READ, handle, 0);
    if (!file) { rc = DL_NO_SPACE; goto done; }
    o = &objects[used_objects];
    if (dl_image_read(&o->plan, (const unsigned char *)(uintptr_t)file, size,
                        DL_IMAGE_DSO, &dl_x32_image_abi) || o->plan.base > PTOS_MMAP_BASE ||
        (uint64_t)o->plan.size + o->plan.align - 4096 > PTOS_MMAP_LIMIT - PTOS_MMAP_BASE) goto done;
    reserve = o->plan.size + o->plan.align - 4096;
    o->reservation = map(reserve); o->reservation_size = reserve;
    if (!o->reservation) { rc = DL_NO_SPACE; goto done; }
    address = o->plan.base + ((o->reservation - o->plan.base + o->plan.align - 1) & ~(o->plan.align - 1));
    if ((uint64_t)address + o->plan.size > (uint64_t)o->reservation + reserve ||
        !populate(o, file, address, handle) ||
        !o->object.has_soname || !equal(dl_string(&o->object, o->object.soname), name)) {
        unmap(o->reservation, o->reservation_size); o->reservation = 0; goto done;
    }
    writable(o);
    o->identity = identity;
    for (i = 0; i < sizeof(canon); i++) o->canonical[i] = canon[i];
    mapping->object = o->object; mapping->identity = identity; mapping->cookie = o;
    used_objects++;
    rc = DL_OK;
done:
    unmap(file, size);
    if (dl_syscall(0x3e, handle, 0, 0, 0, 0, 0) < 0 && rc == DL_OK) {
        OBJECT *last = &objects[--used_objects];
        unmap(last->reservation, last->reservation_size); last->reservation = 0;
        rc = DL_BAD_ELF;
    }
    return rc;
}
static void release(void *context, const DL_MAPPING *mapping)
{
    OBJECT *o = mapping->cookie;
    (void)context;
    unmap(o->reservation, o->reservation_size); o->reservation = 0;
}
static const DL_GRAPH_OPS operations = { acquire, release };

static int executable(uint32_t address)
{
    uint32_t i;
    for (i = 0; i < used_objects; i++) {
        OBJECT *o = &objects[i];
        if (address >= o->object.bias &&
            dl_image_range(&o->plan, address - o->object.bias, 1, DL_PF_X)) return 1;
    }
    return 0;
}
static int hooks(OBJECT *o, int root)
{
    uint32_t i, seen = 0, tag, value, *slot = 0, bit;
    HOOKS *h = &o->hooks;
    const unsigned char *dynamic = o->object.image + o->object.dynamic - o->object.link_base;
    for (i = 0; i < o->object.dynamic_size; i += 8) {
        tag = word(dynamic + i); value = word(dynamic + i + 4); bit = 0;
        if (!tag) break;
        switch (tag) {
        case 12: slot = &h->init; bit = 1; break;
        case 13: slot = &h->fini; bit = 2; break;
        case 25: slot = &h->init_array; bit = 4; break;
        case 27: slot = &h->init_size; bit = 8; break;
        case 26: slot = &h->fini_array; bit = 16; break;
        case 28: slot = &h->fini_size; bit = 32; break;
        case 32: slot = &h->pre_array; bit = 64; break;
        case 33: slot = &h->pre_size; bit = 128; break;
        }
        if (seen & bit) return 0;
        seen |= bit;
        if (bit) *slot = value;
    }
    if (((seen & 12) && (seen & 12) != 12) || ((seen & 48) && (seen & 48) != 48) ||
        ((seen & 192) && ((seen & 192) != 192 || !root))) return 0;
    if ((h->init && (uint64_t)h->init + o->object.bias > UINT32_MAX) ||
        (h->fini && (uint64_t)h->fini + o->object.bias > UINT32_MAX)) return 0;
    if ((h->init && !executable(h->init + o->object.bias)) ||
        (h->fini && !executable(h->fini + o->object.bias))) return 0;
    for (i = 0; i < 3; i++) {
        uint32_t at = i == 0 ? h->init_array : i == 1 ? h->fini_array : h->pre_array;
        uint32_t size = i == 0 ? h->init_size : i == 1 ? h->fini_size : h->pre_size;
        uint32_t j;
        if (size % 4 || size / 4 > MAX_HOOKS ||
            (size && !dl_image_file_range(&o->plan, at, size, DL_PF_R))) return 0;
        for (j = 0; j < size; j += 4)
            if (!executable(word(o->object.image + at - o->object.link_base + j))) return 0;
    }
    return 1;
}
static void init_call(uint32_t address)
{
    ((void (*)(uint32_t, const uint32_t *, const uint32_t *))(uintptr_t)address)(argc, argv, envp);
}
static void init_array(OBJECT *o, uint32_t at, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size; i += 4) init_call(word(o->object.image + at - o->object.link_base + i));
}
void dl_runtime_fini(void)
{
    uint32_t i, j;
    if (finalized) return;
    finalized = 1;
    for (i = initialized; i; i--) {
        OBJECT *o = graph.nodes[graph.order[i - 1]].mapping.cookie;
        for (j = o->hooks.fini_size; j; j -= 4)
            ((void (*)(void))(uintptr_t)word(o->object.image + o->hooks.fini_array - o->object.link_base + j - 4))();
        if (o->hooks.fini) ((void (*)(void))(uintptr_t)(o->object.bias + o->hooks.fini))();
    }
}
static int seal(OBJECT *o)
{
    uint32_t i;
    if (o->reservation) {
        if (!protect(o->reservation, o->reservation_size, PROT_NONE)) return 0;
        for (i = 0; i < o->plan.nload; i++) {
            DL_LOAD *l = &o->plan.loads[i];
            uint32_t first = l->address & ~4095U;
            uint32_t end = (l->address + l->memsz + 4095) & ~4095U;
            uint32_t prot = PROT_READ;
            if (l->flags & DL_PF_W) prot |= PROT_WRITE;
            if (l->flags & DL_PF_X) prot |= PROT_EXEC;
            if (!protect(o->object.bias + first, end - first, prot)) return 0;
        }
    }
    return protect(o->object.bias + o->plan.relro, o->plan.relro_size, PROT_READ);
}

uint32_t dl_runtime(const uint32_t *stack)
{
    const uint32_t *aux, *v;
    uint32_t i, phdr = 0, phnum = 0, entry = 0, base = 0, bias = UINT32_MAX, written;
    const unsigned char *header;
    DL_MAPPING root;
    DL_LAUNCH *launch = 0;
    const char *execfn = 0;
    if (dl_self_pointer64 != (uint64_t)(uintptr_t)dl_runtime_fini) return fail();
    argc = stack[0]; argv = stack + 1;
    if (argc > 16384 || argv[argc]) return fail();
    envp = argv + argc + 1;
    for (v = envp, i = 0; i < 16384 && *v; i++, v++) ;
    if (i == 16384) return fail();
    aux = v + 1; bootdrive = UINT32_MAX;
    for (i = 0; i < 32 && aux[0]; i++, aux += 2) {
        switch (aux[0]) {
        case 3: phdr = aux[1]; break;
        case 4: if (aux[1] != 32) return fail(); break;
        case 5: phnum = aux[1]; break;
        case 6: if (aux[1] != 4096) return fail(); break;
        case 7: base = aux[1]; break;
        case 9: entry = aux[1]; break;
        case 31: execfn = (const char *)(uintptr_t)aux[1]; break;
        case DL_AT_PTOS_LAUNCH: launch = (DL_LAUNCH *)(uintptr_t)aux[1]; break;
        case DL_AT_PTOS_BOOTDRIVE: bootdrive = aux[1]; break;
        case DL_AT_PTOS_LOADBIAS: bias = aux[1]; break;
        }
    }
    if (i == 32 || !phdr || !phnum || phnum > DL_MAX_PHDRS || !base || !entry || bootdrive >= 26) return fail();
    if (bias == UINT32_MAX || !loaded(&objects[0], (const unsigned char *)(uintptr_t)phdr, phnum, bias)) return fail();
    header = (const unsigned char *)(uintptr_t)base;
    if (!loaded(&self, header + word(header + 28), header[44] | ((uint32_t)header[45] << 8), base)) return fail();
    /* A relocated backend function table is used below; bootstrap must have
     * completed before these pointers can be called. */
    root.object = objects[0].object; root.identity = 1; root.cookie = &objects[0];
    used_objects = 1;
    if (dl_graph_open(&graph, &root, &operations, 0)) return fail();
    for (i = 0; i < graph.count; i++) {
        OBJECT *o = graph.nodes[i].mapping.cookie;
        error_name = i ? dl_string(&o->object, o->object.soname) : "main executable";
        if (dl_relocate(&o->object, (unsigned char *)o->object.image, graph.scope, graph.count,
                        o->writable, o->nwritable, work, DL_MAX_RELOCATIONS, &written,
                        &dl_x32_reloc_backend)) goto error;
    }
    for (i = 0; i < graph.count; i++) {
        OBJECT *o = graph.nodes[i].mapping.cookie;
        error_name = i ? dl_string(&o->object, o->object.soname) : "main executable";
        if (!hooks(o, i == 0) || !seal(o)) goto error;
    }
    if (!seal(&self) || !executable(entry)) goto error;
    dl_capture_launch(launch,execfn);
    init_array(&objects[0], objects[0].hooks.pre_array, objects[0].hooks.pre_size);
    for (i = 0; i < graph.order_count; i++) {
        OBJECT *o = graph.nodes[graph.order[i]].mapping.cookie;
        if (o->hooks.init) init_call(o->object.bias + o->hooks.init);
        init_array(o, o->hooks.init_array, o->hooks.init_size);
        initialized++;
    }
    return entry;
error:
    /* Print before releasing mappings that own the diagnostic name. */
    fail(); dl_graph_close(&graph, &operations, 0);
    return 0;
}
