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
 * files.  This file replaces that one (CONF_WITH_VDI_SCALABLE_FONTS, #573),
 * and gives FreeType the three things the VDI's surroundings do not:
 *
 * - memory the kernel owns.  GEMDOS memory belongs to the process that is
 *   running, and the VDI runs for whichever program called it, so faces,
 *   sizes and glyphs must not come from there.  The x86-64 kernel has a
 *   growable kernel heap (kheap.h); elsewhere a pool of
 *   CONF_VDI_FONT_MEMORY KB is set aside while the system starts.
 *
 * - font files that outlive the call that opened them.  A file is read
 *   whole into that memory, with the GEMDOS file functions called directly
 *   (bdos/kfile.c: the VDI is in supervisor mode already, so no trap), and
 *   closed again in the same call: a GEMDOS handle too belongs to the
 *   process that opened it.
 *
 * - a stack of its own.  VDI calls run on whatever stack their caller
 *   has, which can be one of the AES's small per-process stacks or the
 *   2 KB BIOS stack, and FreeType needs far more (its renderer alone puts
 *   16 KB on the stack).  vdi_ft_call() runs a function on FreeType's
 *   stack; everything that calls FreeType goes through it.
 */

#define FT2_BUILD_LIBRARY   /* this is part of FreeType's system layer */

#include "emutos.h"
#include "biosext.h"
#include "../bdos/bdosstub.h"
#include "kprint.h"
#include "string.h"
#include "vdistub.h"
#include "vdi_ftsystem.h"
#if CONF_WITH_KHEAP
#include "kheap.h"
#endif

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftfntfmt.h>
#include <freetype/internal/ftobjs.h>
#include <freetype/internal/ftstream.h>

FT_Library vdi_ft_library;


/*
 * Memory
 *
 * Every block starts with a header holding its size, so that a block can
 * be freed without being told its size (FreeType's free does not say), and
 * so that the memory in use can be counted the same way on every machine.
 */

typedef struct fm_block {
    unsigned long size;             /* of the whole block, header included */
    struct fm_block *next;          /* the next free block (pool only) */
} FM_BLOCK;

#define FM_ALIGN        (2 * sizeof(void *))    /* 8, or 16 on x86-64 */
#define FM_HEADER       ((sizeof(FM_BLOCK) + FM_ALIGN - 1) & ~(FM_ALIGN - 1))
#define FM_ROUND(n)     (((n) + FM_ALIGN - 1) & ~(unsigned long)(FM_ALIGN - 1))

static unsigned long fm_used;       /* bytes in use, headers included */
static unsigned long fm_peak;       /* the most there ever was */
static unsigned long fm_failures;   /* allocations that could not be met */

#if CONF_WITH_KHEAP

static BOOL fm_init(void)
{
    return TRUE;
}

static FM_BLOCK *fm_get(unsigned long size)
{
    FM_BLOCK *b = kalloc(size);

    if (b)
        b->size = size;

    return b;
}

static void fm_put(FM_BLOCK *b)
{
    kfree(b);
}

static unsigned long fm_total(void)
{
    return 0;                       /* growable */
}

#else /* !CONF_WITH_KHEAP */

/*
 * a first fit allocator in one pool, with the free blocks in address
 * order so that neighbours can be merged when a block is freed
 */
static UBYTE *fm_pool;
static unsigned long fm_pool_size;
static FM_BLOCK *fm_free_list;

static BOOL fm_init(void)
{
    fm_pool_size = FM_ROUND(CONF_VDI_FONT_MEMORY * 1024UL);
    fm_pool = balloc_stram(fm_pool_size + FM_ALIGN, FALSE);
    fm_pool = (UBYTE *)FM_ROUND((unsigned long)fm_pool);

    fm_free_list = (FM_BLOCK *)fm_pool;
    fm_free_list->size = fm_pool_size;
    fm_free_list->next = NULL;

    return TRUE;
}

static FM_BLOCK *fm_get(unsigned long size)
{
    FM_BLOCK **link, *b, *rest;

    for (link = &fm_free_list; (b = *link) != NULL; link = &b->next)
    {
        if (b->size < size)
            continue;
        if (b->size - size >= FM_HEADER + FM_ALIGN)
        {
            /* split it: the rest stays free, in the same place in the list */
            rest = (FM_BLOCK *)((UBYTE *)b + size);
            rest->size = b->size - size;
            rest->next = b->next;
            *link = rest;
            b->size = size;
        }
        else
            *link = b->next;        /* take it whole */
        return b;
    }

    return NULL;
}

static void fm_put(FM_BLOCK *b)
{
    FM_BLOCK *prev = NULL, *next = fm_free_list;

    while (next && next < b)
    {
        prev = next;
        next = next->next;
    }

    /* merge with the following free block, then with the preceding one */
    if (next && (UBYTE *)b + b->size == (UBYTE *)next)
    {
        b->size += next->size;
        next = next->next;
    }
    b->next = next;
    if (prev && (UBYTE *)prev + prev->size == (UBYTE *)b)
    {
        prev->size += b->size;
        prev->next = next;
    }
    else if (prev)
        prev->next = b;
    else
        fm_free_list = b;
}

static unsigned long fm_total(void)
{
    return fm_pool_size;
}

#endif /* CONF_WITH_KHEAP */

static void *fm_alloc(unsigned long size)
{
    FM_BLOCK *b;

    if (!size)
        return NULL;
    size = FM_HEADER + FM_ROUND(size);
    b = fm_get(size);
    if (!b)
    {
        fm_failures++;
        return NULL;
    }
    fm_used += b->size;
    if (fm_used > fm_peak)
        fm_peak = fm_used;

    return (UBYTE *)b + FM_HEADER;
}

static void fm_free(void *p)
{
    FM_BLOCK *b;

    if (!p)
        return;
    b = (FM_BLOCK *)((UBYTE *)p - FM_HEADER);
    fm_used -= b->size;
    fm_put(b);
}

/* how much a block can hold */
static unsigned long fm_capacity(void *p)
{
    return ((FM_BLOCK *)((UBYTE *)p - FM_HEADER))->size - FM_HEADER;
}


/* FreeType's view of it */

static void *ft_alloc(FT_Memory memory, long size)
{
    UNUSED(memory);

    return (size > 0) ? fm_alloc(size) : NULL;
}

static void ft_free(FT_Memory memory, void *block)
{
    UNUSED(memory);
    fm_free(block);
}

static void *ft_realloc(FT_Memory memory, long cur_size, long new_size,
                        void *block)
{
    void *p;

    UNUSED(memory);
    if (!block)
        return ft_alloc(memory, new_size);
    if (new_size <= 0)
    {
        fm_free(block);
        return NULL;
    }
    if ((unsigned long)new_size <= fm_capacity(block))
        return block;

    p = fm_alloc(new_size);
    if (p)
    {
        memcpy(p, block, cur_size);
        fm_free(block);
    }

    return p;
}

/* filled in at run time, so that it is not in the ROM's data segment */
static struct FT_MemoryRec_ ft_memory;

FT_BASE_DEF( FT_Memory )
FT_New_Memory(void)
{
    ft_memory.user = NULL;
    ft_memory.alloc = ft_alloc;
    ft_memory.free = ft_free;
    ft_memory.realloc = ft_realloc;

    return &ft_memory;
}

FT_BASE_DEF( void )
FT_Done_Memory(FT_Memory memory)
{
    UNUSED(memory);
}


/*
 * Font files
 *
 * read whole into kernel memory, which goes back when FreeType closes the
 * stream (the face is done with)
 */

static void ft_stream_close(FT_Stream stream)
{
    fm_free(stream->base);
    stream->base = NULL;
    stream->size = 0;
}

FT_BASE_DEF( FT_Error )
FT_Stream_Open(FT_Stream stream, const char *filepathname)
{
    LONG handle, size, n;
    UBYTE *buf;

    if (!stream)
        return FT_Err_Invalid_Stream_Handle;

    handle = kdos_fopen((long)filepathname, 0);
    if (handle < 0)
        return FT_Err_Cannot_Open_Resource;

    size = kdos_fseek(0L, handle, 2);
    if (size <= 0 || kdos_fseek(0L, handle, 0) != 0)
    {
        kdos_fclose(handle);
        return FT_Err_Cannot_Open_Stream;
    }

    buf = fm_alloc(size);
    if (!buf)
    {
        kdos_fclose(handle);
        return FT_Err_Out_Of_Memory;
    }

    n = kdos_fread(handle, size, (long)buf);
    kdos_fclose(handle);
    if (n != size)
    {
        fm_free(buf);
        return FT_Err_Cannot_Open_Stream;
    }

    stream->base = buf;
    stream->size = size;
    stream->pos = 0;
    stream->descriptor.pointer = buf;
    stream->pathname.pointer = (char *)filepathname;
    stream->read = NULL;
    stream->close = ft_stream_close;

    return FT_Err_Ok;
}


/*
 * FreeType's stack
 *
 * Its size covers the renderer's 16 KB buffer (FT_RENDER_POOL_SIZE) and
 * the depth of the TrueType bytecode interpreter and the CFF hinter, with
 * room to spare; vdi_freetype_check() reports how much was used.  It is
 * filled with a pattern when it is made, so that the depth reached can be
 * read off it.
 */

#define FT_STACK_SIZE   (64 * 1024UL)
#define FT_STACK_FILL   0xa5

static UBYTE *ft_stack;
static UBYTE *ft_stack_top;
static BOOL ft_on_stack;

static long (*ft_call_fn)(void *);
static void *ft_call_arg;
static long ft_call_result;

/* what runs on FreeType's stack: a function of no arguments and no result,
 * the same with any calling convention, so that the switch below needs
 * to know nothing of them */
static void ft_trampoline(void)
{
    ft_call_result = ft_call_fn(ft_call_arg);
}

/*
 * switch to the stack whose top is 'top', call 'f', and switch back.  The
 * old stack pointer is kept on the new stack (m68k) or in a register the
 * callee saves (ARM, x86-64).  The clobbers are the registers a called
 * function may change under any of pTOS's calling conventions (m68k with
 * or without -mfastcall, AAPCS with hard float, SysV x86-64).
 */
static void ft_switch_and_call(UBYTE *top, void (*f)(void))
{
#if defined(__mc68000__) || defined(__mcoldfire__)
    __asm__ volatile
    (
        "move.l  sp,a0\n\t"
        "move.l  %0,sp\n\t"
        "move.l  a0,-(sp)\n\t"
        "jsr     (%1)\n\t"
        "move.l  (sp),sp"
    :
    : "d"(top), "a"(f)
    : "d0", "d1", "d2", "a0", "a1", "memory", "cc"
    );
#elif defined(__arm__)
    __asm__ volatile
    (
        "mov     r4, sp\n\t"
        "mov     sp, %0\n\t"
        "blx     %1\n\t"
        "mov     sp, r4"
    :
    : "r"(top), "r"(f)
    : "r0", "r1", "r2", "r3", "r4", "r12", "lr", "memory", "cc"
# ifdef __ARM_PCS_VFP
      , "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7"
#  if defined(__ARM_NEON) || defined(__ARM_NEON__)
      , "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23"
      , "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"
#  endif
# endif
    );
#elif defined(__x86_64__)
    __asm__ volatile
    (
        "mov     %%rsp, %%rbx\n\t"
        "mov     %0, %%rsp\n\t"
        "call    *%1\n\t"
        "mov     %%rbx, %%rsp"
    :
    : "r"(top), "r"(f)
    : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
      "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
      "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
      "memory", "cc"
    );
#else
# error "no FreeType stack switch for this architecture"
#endif
}

/*
 * run fn(arg) on FreeType's stack and return its result.  A call from
 * there (FreeType calling back into pTOS, which calls FreeType again)
 * runs on directly.
 */
long vdi_ft_call(long (*fn)(void *), void *arg)
{
    if (ft_on_stack)
        return fn(arg);

    ft_call_fn = fn;
    ft_call_arg = arg;
    ft_on_stack = TRUE;
    ft_switch_and_call(ft_stack_top, ft_trampoline);
    ft_on_stack = FALSE;

    return ft_call_result;
}


/*
 * Setting up
 */

static long ft_init(void *arg)
{
    UNUSED(arg);

    return FT_Init_FreeType(&vdi_ft_library);
}

/*
 * called once while the BIOS starts, before GEMDOS takes the memory that
 * is left (the pool comes from balloc_stram()): set FreeType up for the
 * VDI, on every configuration with the scalable fonts, whether or not
 * anything opens a VDI workstation
 */
void vdi_freetype_init(void)
{
    FT_Int major, minor, patch;
    FT_Error error;

    if (!fm_init())
        return;

    ft_stack = fm_alloc(FT_STACK_SIZE);
    if (!ft_stack)
    {
        KINFO(("FreeType: no memory for its stack\n"));
        return;
    }
    memset(ft_stack, FT_STACK_FILL, FT_STACK_SIZE);
    ft_stack_top = (UBYTE *)((unsigned long)(ft_stack + FT_STACK_SIZE)
                             & ~(unsigned long)15);  /* any ABI's alignment */

    error = vdi_ft_call(ft_init, NULL);
    if (error)
    {
        KINFO(("FreeType: FT_Init_FreeType() failed: %d\n", (int)error));
        vdi_ft_library = NULL;
        return;
    }

    FT_Library_Version(vdi_ft_library, &major, &minor, &patch);
    if (fm_total())
        KINFO(("FreeType %d.%d.%d, %lu of %lu KB of font memory used\n",
               (int)major, (int)minor, (int)patch, fm_used / 1024, fm_total() / 1024));
    else
        KINFO(("FreeType %d.%d.%d, %lu KB of the kernel heap used\n",
               (int)major, (int)minor, (int)patch, fm_used / 1024));
}


#ifdef CONF_DEBUG_FREETYPE_TEST_FONT

/*
 * Checking the port layer with a font file (CONF_DEBUG_FREETYPE_TEST_FONT):
 * open it, render a few glyphs at a few sizes, close it, and report.
 */

/* the deepest FreeType's stack has been used, in bytes */
static unsigned long ft_stack_depth(void)
{
    UBYTE *p = ft_stack;

    while (p < ft_stack_top && *p == FT_STACK_FILL)
        p++;

    return ft_stack_top - p;
}

static long ft_test(void *arg)
{
    static const char glyphs[] = "Ag@";
    static const int points[] = { 8, 12, 24, 72 };
    const char *path = arg;
    FT_Face face;
    FT_Error error;
    int i;
    const char *s;

    error = FT_New_Face(vdi_ft_library, path, 0, &face);
    if (error)
    {
        KINFO(("FreeType: cannot open %s: error %d\n", path, (int)error));
        return error;
    }
    KINFO(("FreeType: %s: %s %s, %ld glyphs, %d charmaps, %s\n", path,
           face->family_name ? face->family_name : "?",
           face->style_name ? face->style_name : "?",
           (long)face->num_glyphs, (int)face->num_charmaps,
           FT_Get_Font_Format(face)));

    for (i = 0; i < (int)(sizeof(points) / sizeof(points[0])); i++)
    {
        error = FT_Set_Char_Size(face, 0, points[i] * 64, 72, 72);
        if (error)
            break;
        for (s = glyphs; *s; s++)
        {
            error = FT_Load_Char(face, (FT_ULong)(UBYTE)*s,
                                 FT_LOAD_RENDER | FT_LOAD_TARGET_MONO);
            if (error)
                break;
            KINFO(("FreeType:   %2dpt '%c': %dx%d, advance %ld\n", points[i], *s,
                   (int)face->glyph->bitmap.width, (int)face->glyph->bitmap.rows,
                   (long)(face->glyph->advance.x >> 6)));
        }
        if (error)
            break;
    }
    if (error)
        KINFO(("FreeType: rendering failed: error %d\n", (int)error));

    FT_Done_Face(face);

    return error;
}

/* called once GEMDOS has the boot drive */
void vdi_freetype_test_font(void)
{
    unsigned long before;

    if (!vdi_ft_library)
        return;

    before = fm_used;
    vdi_ft_call(ft_test, CONF_DEBUG_FREETYPE_TEST_FONT);
    KINFO(("FreeType: peak %lu KB, %lu bytes not given back, %lu failed allocations, stack %lu of %lu bytes deep\n",
           fm_peak / 1024, fm_used - before, fm_failures, ft_stack_depth(),
           (unsigned long)FT_STACK_SIZE));
}

#endif /* CONF_DEBUG_FREETYPE_TEST_FONT */
