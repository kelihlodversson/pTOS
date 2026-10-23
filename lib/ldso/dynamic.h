/*
 * dynamic.h - userspace ELF32 dependency and symbol metadata (#660)
 *
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LDSO_DYNAMIC_H
#define LDSO_DYNAMIC_H

#include <stdint.h>

#define DL_MAX_NEEDED 32

#define DL_OK           0
#define DL_NOT_FOUND    1
#define DL_BAD_ELF     -1
#define DL_UNSUPPORTED -2

/* Native-endian ELF32, copied to avoid alignment and aliasing assumptions. */
typedef struct {
    uint32_t name;
    uint32_t value;
    uint32_t size;
    unsigned char info;
    unsigned char other;
    uint16_t shndx;
} DL_SYMBOL;

/*
 * A fully readable, contiguous image, with holes zero-filled by the mapper.
 * image[0] represents link_base, not the ELF file's first byte.  bias is
 * separate from the host pointer so tests can inspect target ELF objects.
 * The mapping and dynamic table must remain valid for this view's lifetime.
 * The eventual mapper must validate PT_LOAD and enforce its permissions;
 * this module neither maps pages nor applies relocations.
 */
typedef struct {
    const unsigned char *image;
    uint32_t image_size;
    uint32_t link_base;
    uint32_t bias;
    uint32_t symtab;
    uint32_t strtab;
    uint32_t strsz;
    uint32_t hash;
    uint32_t nbucket;
    uint32_t nchain;
    uint32_t needed[DL_MAX_NEEDED];
    uint32_t needed_count;
    uint32_t soname;
    int has_soname;
} DL_OBJECT;

/* dyn_vaddr is the PT_DYNAMIC virtual address; dyn_size is its file size.
 * Output is cleared even on failure.  SysV DT_HASH is the initial subset. */
int dl_read_dynamic(DL_OBJECT *out, const unsigned char *image,
                    uint32_t image_size, uint32_t link_base, uint32_t bias,
                    uint32_t dyn_vaddr, uint32_t dyn_size);

/* A validated, terminated string within DT_STRTAB, or NULL. */
const char *dl_string(const DL_OBJECT *object, uint32_t offset);

/* One object's exported symbol, not a global resolution policy.  Hidden,
 * internal, local and undefined symbols are excluded.  Weak and protected
 * definitions are eligible; caller order determines interposition.  A found
 * symbol of an unsupported kind (TLS, IFUNC, COMMON) fails explicitly.
 * On success, address is a target address (SHN_ABS is not biased).
 * Output parameters are unchanged on failure. */
int dl_find_symbol(const DL_OBJECT *object, const char *name,
                    DL_SYMBOL *symbol, uint32_t *address);

#endif
