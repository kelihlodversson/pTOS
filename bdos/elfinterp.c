/*
 * elfinterp.c - standard ELF interpreter loading, no symbol resolution (#660)
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "emutos.h"
#include "fs.h"
#include "gemerror.h"
#include "kheap.h"
#include "tosvars.h"
#include "bdosstub.h"
#include "kproc.h"
#include "elfinterp.h"
#include "../lib/ldso/image.h"

/* Bound transient kernel storage; file data is freed before launch. */
#define INTERP_FILE_LIMIT 0x00800000L

static LONG read_at(FH h, ULONG offset, void *buffer, LONG count)
{
    LONG rc;
    if (offset > 0x7fffffffUL)
        return EPLFMT;
    rc = kdos_fseek(offset, h, 0);
    if (rc < 0)
        return rc;
    rc = kdos_fread(h, count, (long)buffer);
    return rc < 0 ? rc : rc == count ? E_OK : EPLFMT;
}

static LONG file_image(FH h, UBYTE **buffer, ULONG *size)
{
    LONG rc = kdos_fseek(0, h, 2);
    if (rc < 0)
        return rc;
    if (rc < 52)
        return EPLFMT;
    if (rc > INTERP_FILE_LIMIT)
        return ENSMEM;
    *size = rc;
    *buffer = kalloc(*size);
    if (!*buffer)
        return ENSMEM;
    return read_at(h, 0, *buffer, *size);
}

/* a field of the ELF header or of a program header, in the byte order of the
 * file (EI_DATA: 1 little endian, 2 big endian) */
static ULONG word(const UBYTE *p, BOOL big)
{
    if (big)
        return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | (ULONG)p[3];
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}

static ULONG half(const UBYTE *p, BOOL big)
{
    return big ? ((ULONG)p[0] << 8) | p[1] : (ULONG)p[0] | ((ULONG)p[1] << 8);
}

LONG elf_interp_load(FH h, PD *pd, const char *filename)
{
    UBYTE header[52], ph[32];
    char pathname[DL_MAX_INTERP + 3];
    UBYTE *program = NULL, *interpreter = NULL;
    DL_IMAGE main_plan, interp_plan;
    ULONG phoff, entsize, count, i, main_size = 0, interp_size = 0;
    LONG rc, handle = -1;
    BOOL big;

    rc = read_at(h, 0, header, sizeof(header));
    if (rc)
        return rc;
    /* The sizing pass has checked class/machine/endian. Do not parse an
     * unbounded or overflowing program-header table even for dispatch. */
    big = header[5] == 2;
    phoff = word(header + 28, big);
    entsize = half(header + 42, big);
    count = half(header + 44, big);
    if (!count || entsize < 32 ||
        phoff > 0x7fffffffUL || count * entsize > 0x7fffffffUL - phoff)
        return EPLFMT;
    for (i = 0; i < count; i++) {
        rc = read_at(h, phoff + i * entsize, ph, sizeof(ph));
        if (rc)
            return rc;
        if (word(ph, big) == 3)
            break;
    }
    if (i == count)
        return 1;                       /* ordinary static ELF/PIE */
    rc = file_image(h, &program, &main_size);
    if (rc)
        goto done;
    if (dl_image_read(&main_plan, program, main_size,
                        DL_IMAGE_EXECUTABLE, &dl_image_abi)) {
        rc = EPLFMT;
        goto done;
    }
    /* ELF absolute paths are rooted on the boot filesystem. Translate
     * separators for GEMDOS explicitly; never inherit a caller's drive/CWD. */
    if (bootdev < 0 || bootdev >= 26) {
        rc = EPLFMT;
        goto done;
    }
    pathname[0] = 'A' + bootdev;
    pathname[1] = ':';
    for (i = 0; i < main_plan.interp_size; i++) {
        UBYTE c = program[main_plan.interp_offset + i];
        if (c == ':' || c == '\\' || (c && (c < 32 || c == 127))) {
            rc = EPLFMT;
            goto done;
        }
        pathname[i + 2] = c == '/' ? '\\' : c;
    }
    handle = kdos_fopen((long)pathname, 0);
    if (handle < 0) {
        rc = handle;
        goto done;
    }
    rc = file_image(handle, &interpreter, &interp_size);
    if (rc)
        goto done;
    if (dl_image_read(&interp_plan, interpreter, interp_size,
                        DL_IMAGE_INTERPRETER, &dl_image_abi)) {
        rc = EPLFMT;
        goto done;
    }
    rc = kproc_set_interpreter(pd, &main_plan, program, &interp_plan, interpreter, filename);
done:
    if (handle >= 0) {
        LONG closed = kdos_fclose(handle);
        if (!rc && closed < 0)
            rc = closed;
    }
    kfree(interpreter);
    kfree(program);
    return rc;
}
