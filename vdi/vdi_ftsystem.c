/*
 * vdi_ftsystem.c - pTOS's system layer for FreeType
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * FreeType reaches the system through three functions, which its own
 * src/base/ftsystem.c implements with the host's C library: FT_New_Memory()
 * and FT_Done_Memory() for its memory, and FT_Stream_Open() for font
 * files.  This file replaces that one (CONF_WITH_VDI_SCALABLE_FONTS, #573).
 *
 * For now (#574) FreeType is only set up and shut down again once, to
 * check that it works, in a small static arena.  Memory the kernel owns,
 * that outlives the process the VDI happens to run for (GEMDOS memory
 * does not, and is not there for the kernel on every architecture), and
 * font files come with #575.
 */

#define FT2_BUILD_LIBRARY   /* this is part of FreeType's system layer */

#include "emutos.h"
#include "kprint.h"
#include "string.h"
#include "vdistub.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/internal/ftobjs.h>
#include <freetype/internal/ftstream.h>

/*
 * a bump allocator in a static block: freeing does nothing, the whole
 * block is reused at once.  Setting FreeType up takes some 650 bytes on
 * 32-bit CPUs.
 */
#define ARENA_SIZE      4096
#define ARENA_ALIGN     8               /* enough for any type FreeType uses */

static UBYTE arena[ARENA_SIZE] __attribute__((aligned(ARENA_ALIGN)));
static UBYTE *arena_next, *arena_end;

static void *arena_alloc(FT_Memory memory, long size)
{
    UBYTE *p = arena_next;

    UNUSED(memory);
    size = (size + ARENA_ALIGN - 1) & ~(long)(ARENA_ALIGN - 1);
    if (size <= 0 || size > arena_end - p)
        return NULL;
    arena_next = p + size;

    return p;
}

static void arena_free(FT_Memory memory, void *block)
{
    UNUSED(memory);
    UNUSED(block);
}

static void *arena_realloc(FT_Memory memory, long cur_size, long new_size,
                           void *block)
{
    void *p;

    if (new_size <= cur_size)
        return block;
    p = arena_alloc(memory, new_size);
    if (p && block)
        memcpy(p, block, cur_size);

    return p;
}

/* filled in at run time, so that it is not in the ROM's data segment */
static struct FT_MemoryRec_ arena_memory;


FT_BASE_DEF( FT_Memory )
FT_New_Memory(void)
{
    arena_memory.user = NULL;
    arena_memory.alloc = arena_alloc;
    arena_memory.free = arena_free;
    arena_memory.realloc = arena_realloc;

    return &arena_memory;
}


FT_BASE_DEF( void )
FT_Done_Memory(FT_Memory memory)
{
    UNUSED(memory);
}


/* there are no font files yet */
FT_BASE_DEF( FT_Error )
FT_Stream_Open(FT_Stream stream, const char *filepathname)
{
    UNUSED(stream);
    UNUSED(filepathname);

    return FT_Err_Cannot_Open_Resource;
}


/*
 * set FreeType up and shut it down again, and say which version it is:
 * called once while the BIOS starts, on every configuration with the
 * scalable fonts, whether or not anything opens a VDI workstation
 */
void vdi_freetype_check(void)
{
    FT_Library library;
    FT_Error error;
    FT_Int major, minor, patch;

    arena_next = arena;
    arena_end = arena + ARENA_SIZE;

    error = FT_Init_FreeType(&library);
    if (error)
        KINFO(("FreeType: FT_Init_FreeType() failed: %d\n", (int)error));
    else
    {
        FT_Library_Version(library, &major, &minor, &patch);
        KINFO(("FreeType %d.%d.%d, %ld bytes\n", (int)major, (int)minor,
               (int)patch, (long)(arena_next - arena)));
        FT_Done_FreeType(library);
    }

    arena_next = arena_end = NULL;
}
