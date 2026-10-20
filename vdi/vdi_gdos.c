/*
 * vdi_gdos.c - the built-in GDOS layer for the scalable fonts
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Applications only ask for fonts beyond the system fonts when they find
 * a GDOS: vq_gdos() (trap #2 with d0 = -2) must say there is one, and
 * those that use outline fonts look for the FSMC cookie as well.  Then
 * they call vst_load_fonts() and list the fonts with vqt_name().  With
 * CONF_WITH_VDI_SCALABLE_FONTS, pTOS answers them as SpeedoGDOS does
 * (#582):
 *
 * - vq_gdos() returns '_FSM', the answer of SpeedoGDOS (and of its
 *   predecessor FSMGDOS, whose interface it kept), and the FSMC cookie
 *   points to a structure with the magic '_SPD', which is how
 *   applications tell SpeedoGDOS from FSMGDOS.  Its version is 4.00, the
 *   first SpeedoGDOS, which introduced the outline flag of vqt_name().
 *
 * - vst_load_fonts() adds the fonts found here to the workstation and
 *   returns how many, and vqt_name() lists them after the system fonts,
 *   flagged as outline fonts (vdi/vdi_text.c).
 *
 * The fonts are the TrueType (*.TTF), OpenType (*.OTF) and TrueType
 * collection (*.TTC, each of its faces) files in CONF_VDI_FONT_DIR, which
 * is scanned once, as the AUTO folder has run: a missing or empty
 * directory just leaves the system without scalable fonts, and if the
 * scan found none the first vst_load_fonts() looks again.  Each gets a font id from a hash of its full family
 * and style names, in a range of its own (SFONT_ID_FIRST on), so that a
 * document which stores a font id finds the same font at the next boot.
 * When two fonts hash to the same id, the one that sorts later takes the
 * next free one.  The hash is of the names as the font has them, not of
 * the 32 characters vqt_name() returns: two faces whose names only differ
 * past those stay two fonts with ids of their own.
 *
 * A GDOS loaded from disk (NVDI, SpeedoGDOS, FSMGDOS, FontGDOS, ... in the
 * AUTO folder) takes precedence: the built-in one only starts once the
 * AUTO folder has run, and stays out altogether if trap #2 has been
 * hooked by then, or if an FSMC or NVDI cookie is there.  That GDOS then
 * answers vq_gdos() in front of the ROM, uses the VDI as a driver as
 * before, and gets no scalable fonts of pTOS's underneath its own.
 * GEM bitmap fonts (*.FNT, listed in ASSIGN.SYS) are left to such a
 * GDOS too: the built-in one does not load them.
 *
 * On x86-64 only the kernel itself can call the VDI (trap class 2 is
 * refused from ring 3), and the cookie jar holds 32-bit values, so there
 * is no FSMC cookie there: the GDOS is just active, and vq_gdos() answers
 * '_FSM' to a kernel caller.
 */

#include "emutos.h"
#include "biosext.h"
#include "cookie.h"
#include "kprint.h"
#include "stdlib.h"
#include "string.h"
#include "tosvars.h"
#include "../bdos/bdosstub.h"
#include "../bios/vectors.h"
#include "../bios/font.h"
#include "../bios/country.h"
#include "vdistub.h"
#include "vdi_ftsystem.h"
#include "vdi_gdos.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftfntfmt.h>
#include <freetype/tttables.h>
#include <freetype/tttags.h>

/* an empty option is not defined at all (tools/genconfig.py): the root */
#ifndef CONF_VDI_FONT_DIR
#define CONF_VDI_FONT_DIR ""
#endif

/* an empty option is not defined at all: the ROM fonts stay the system fonts */
#ifndef CONF_VDI_SYSTEM_FONT
#define CONF_VDI_SYSTEM_FONT ""
#endif

ULONG vdi_gdos_id;

WORD vdi_sfont_count;
VDI_SFONT *vdi_sfonts;

#define SFONT_MAX       256         /* fonts considered from the directory */
#define SFONT_ID_FIRST  10000       /* the scalable fonts' ids: past the */
#define SFONT_ID_COUNT  20000       /* system and bitmap GDOS fonts' */

#if !ARCH_X86_64
/* what the FSMC cookie points to */
static struct
{
    ULONG magic;                    /* '_SPD': SpeedoGDOS */
    UWORD version;                  /* 0x0400 for 4.00 */
    WORD quality;                   /* -1, as fVDI's SpeedoGDOS mode has it */
} fsmc;
#endif

/*
 * The directory, made absolute: on the boot drive unless it names one,
 * and from the root.  The files are opened again later on behalf of
 * whichever program uses a font, whose current drive and directory may
 * be anywhere.
 */
static char sfont_dir[3 + sizeof(CONF_VDI_FONT_DIR)];
static char sfont_path[sizeof(sfont_dir) + 16];
static DTA sfont_dta;
static long sfont_faces;            /* faces in the file sfont_open() opened */


/*
 * Opening a font file, on FreeType's stack (vdi_ft_call()): the file is
 * sfont_path, and its name, format and flags go into the VDI_SFONT
 */

/* the names fonts give their regular style (DejaVu calls it "Book") */
static BOOL sfont_is_regular(const char *style)
{
    return !strcmp(style, "Regular") || !strcmp(style, "Book")
        || !strcmp(style, "Roman") || !strcmp(style, "Normal");
}

/*
 * "Family Style", without the style when it is the regular one; f->style
 * says where the style starts, for vqt_xfntinfo()
 */
static void sfont_name(VDI_SFONT *f, const char *family, const char *style)
{
    char *name = f->name, *p = name, *end = name + FONT_NAME_LEN;
    const char *s;

    f->style = 0;
    for (s = family ? family : "?"; *s && p < end; )
        *p++ = *s++;
    if (style && *style && !sfont_is_regular(style) && p < end - 1)
    {
        *p++ = ' ';
        f->style = (UBYTE)(p - name);
        for (s = style; *s && p < end; )
            *p++ = *s++;
    }
    *p = '\0';

    /* only printable ASCII: the name goes out as one WORD per character */
    for (p = name; *p; p++)
        if ((UBYTE)*p < 0x20 || (UBYTE)*p > 0x7e)
            *p = '?';
}

/* the font's identity: its family and style names, whole */
static ULONG sfont_key(const char *family, const char *style)
{
    ULONG h = 5381;
    const char *s;

    for (s = family ? family : ""; *s; s++)
        h = h * 33 + (UBYTE)*s;
    h = h * 33;                     /* a separator, so "AB"+"C" is not "A"+"BC" */
    for (s = style ? style : ""; *s; s++)
        h = h * 33 + (UBYTE)*s;

    return h;
}

/*
 * the pairs in a font's 'kern' table, the only kerning FreeType gives
 * (FT_Get_Kerning()): those of its first subtable, which is what fonts
 * have; 0 for none
 */
static UWORD sfont_kern_pairs(FT_Face face)
{
    FT_Byte head[12];
    FT_ULong len = sizeof(head);

    if (!FT_HAS_KERNING(face) || FT_Load_Sfnt_Table(face, TTAG_kern, 0, head, &len))
        return 0;

    /* version, subtables, then the first one's version, length, coverage, pairs */
    return (UWORD)((head[10] << 8) | head[11]);
}

static long sfont_open(void *arg)
{
    VDI_SFONT *f = arg;
    FT_Face face;
    const char *format;

    if (FT_New_Face(vdi_ft_library, sfont_path, f->face, &face))
        return -1;
    sfont_faces = face->num_faces;
    if (!FT_IS_SCALABLE(face))
    {
        FT_Done_Face(face);
        return -1;
    }

    sfont_name(f, face->family_name, face->style_name);
    f->key = sfont_key(face->family_name, face->style_name);
    format = FT_Get_Font_Format(face);
    f->format = (format && strcmp(format, "TrueType")) ? SFONT_TYPE1 : SFONT_TRUETYPE;
    f->flags = FT_IS_FIXED_WIDTH(face) ? SFONT_MONOSPACE : 0;
    if (face->style_flags & FT_STYLE_FLAG_ITALIC)
        f->flags |= SFONT_ITALIC;
    if (face->style_flags & FT_STYLE_FLAG_BOLD)
        f->flags |= SFONT_BOLD;
    f->upem = face->units_per_EM;
    f->ascender = face->ascender;
    f->glyphs = (face->num_glyphs > 0xffffL) ? 0xffff : (UWORD)face->num_glyphs;
    f->kern_pairs = sfont_kern_pairs(face);

    FT_Done_Face(face);

    return 0;
}


/*
 * Finding the fonts
 */

static void sfont_set_dir(void)
{
    const char *dir = CONF_VDI_FONT_DIR;
    char *p = sfont_dir;

    if (dir[0] && dir[1] == ':')
    {
        *p++ = *dir++;
        *p++ = *dir++;
    }
    else
    {
        *p++ = 'A' + bootdev;
        *p++ = ':';
    }
    if (*dir != '\\')
        *p++ = '\\';
    strcpy(p, dir);
}

static void sfont_set_path(const char *file)
{
    size_t len;

    strcpy(sfont_path, sfont_dir);
    len = strlen(sfont_path);
    if (!len || sfont_path[len - 1] != '\\')   /* "C:\FONTS\" or "C:\" has one */
        strcat(sfont_path, "\\");
    strcat(sfont_path, file);
}

const char *vdi_sfont_path(WORD index)
{
    sfont_set_path(vdi_sfonts[index].file);

    return sfont_path;
}

LONG vdi_sfont_face(WORD index)
{
    return vdi_sfonts[index].face;
}

/* add the fonts matching one pattern; returns how many there are now */
static WORD sfont_find(VDI_SFONT *fonts, WORD count, const char *pattern)
{
    long rc, face;

    sfont_set_path(pattern);
    for (rc = kdos_fsfirst_at((long)sfont_path, 0, &sfont_dta); rc == 0;
         rc = kdos_fsnext_at(&sfont_dta))
    {
        /* go on to the end even when full: the search is then over */
        if (count == SFONT_MAX)
            continue;
        strcpy(fonts[count].file, sfont_dta.d_fname);
        sfont_set_path(sfont_dta.d_fname);
        fonts[count].face = 0;
        sfont_faces = 1;
        if (vdi_ft_call(sfont_open, &fonts[count]) != 0)
        {
            KINFO(("GDOS: %s is not a scalable font FreeType can open\n",
                   sfont_dta.d_fname));
            continue;
        }
        count++;

        /* a collection: the other faces, which are fonts of their own */
        for (face = 1; face < sfont_faces && face < 256 && count < SFONT_MAX; face++)
        {
            strcpy(fonts[count].file, sfont_dta.d_fname);
            fonts[count].face = (UBYTE)face;
            if (vdi_ft_call(sfont_open, &fonts[count]) == 0)
                count++;
        }
    }

    return count;
}

static int sfont_compare(const void *a, const void *b)
{
    const VDI_SFONT *fa = a, *fb = b;
    int rc;

    rc = strcmp(fa->name, fb->name);
    if (!rc && fa->key != fb->key)
        rc = (fa->key < fb->key) ? -1 : 1;

    if (!rc)
        rc = strcmp(fa->file, fb->file);

    return rc ? rc : (int)fa->face - (int)fb->face;
}

static BOOL sfont_id_used(const VDI_SFONT *fonts, WORD count, WORD id)
{
    WORD i;

    for (i = 0; i < count; i++)
        if (fonts[i].id == id)
            return TRUE;

    return FALSE;
}

/* the fonts in CONF_VDI_FONT_DIR, sorted by name, with their ids */
static void sfont_scan(void)
{
    VDI_SFONT *fonts;
    WORD count, i, n;
    UWORD slot;

    fonts = vdi_ft_alloc(SFONT_MAX * sizeof(VDI_SFONT));
    if (!fonts)
    {
        KINFO(("GDOS: no memory to list the scalable fonts\n"));
        return;
    }

    count = sfont_find(fonts, 0, "*.TTF");
    count = sfont_find(fonts, count, "*.OTF");
    count = sfont_find(fonts, count, "*.TTC");
    qsort(fonts, count, sizeof(VDI_SFONT), sfont_compare);

    /* the same font twice (a .TTF and an .OTF, say): keep the first file */
    for (i = n = 0; i < count; i++)
        if (!n || fonts[i].key != fonts[n - 1].key
            || strcmp(fonts[i].name, fonts[n - 1].name))
            fonts[n++] = fonts[i];
    count = n;

    /* the ids, in name order, so that collisions resolve the same way */
    for (i = 0; i < count; i++)
    {
        slot = (UWORD)(fonts[i].key % SFONT_ID_COUNT);
        while (sfont_id_used(fonts, i, (WORD)(SFONT_ID_FIRST + slot)))
            slot = (slot + 1) % SFONT_ID_COUNT;
        fonts[i].id = (WORD)(SFONT_ID_FIRST + slot);
        KINFO(("GDOS: font %d: %s (%s)\n", fonts[i].id, fonts[i].name,
               fonts[i].file));
    }

    if (!count)
    {
        vdi_ft_free(fonts);
        fonts = NULL;
    }
    vdi_sfonts = fonts;
    vdi_sfont_count = count;
}


/*
 * The system font (#600): CONF_VDI_SYSTEM_FONT, or what the line
 * SYSTEMFONT= of \GEMSYS\PTOS.INF on the boot drive says, if there is
 * one, is a font file of the font directory; if it is monospaced, it
 * becomes the 8x8 and 8x16 system fonts.
 */
#define SYSTEM_FONT_FILE    "\\GEMSYS\\PTOS.INF"
#define SYSTEM_FONT_KEY     "SYSTEMFONT"

/* the name that the settings file gives, upper case, in name (14 characters and a NUL); FALSE if none */
static BOOL sfont_system_setting(char *name)
{
    char path[3 + sizeof(SYSTEM_FONT_FILE)], buf[256];
    long h, n;
    char *p, *end;
    const char *key;

    path[0] = 'A' + bootdev;
    path[1] = ':';
    strcpy(path + 2, SYSTEM_FONT_FILE);
    h = kdos_fopen((long)path, 0);
    if (h < 0)
        return FALSE;
    n = kdos_fread(h, (long)sizeof(buf) - 1, (long)buf);
    kdos_fclose(h);
    if (n <= 0)
        return FALSE;
    buf[n] = '\0';

    for (p = buf; *p; )
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        for (key = SYSTEM_FONT_KEY; *key && *p == *key; key++, p++)
            ;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*key && *p == '=')
        {
            for (p++; *p == ' ' || *p == '\t'; p++)
                ;
            for (end = name; *p && *p != '\r' && *p != '\n' && *p != ' ' && *p != '\t'
                             && end < name + 13; p++)
                *end++ = (*p >= 'a' && *p <= 'z') ? (char)(*p - 'a' + 'A') : *p;
            *end = '\0';

            return end != name;
        }
        while (*p && *p != '\n')       /* another line */
            p++;
    }

    return FALSE;
}

static void sfont_system(void)
{
    char name[14] = CONF_VDI_SYSTEM_FONT;
    Fonthead new16 = fon8x16, new8 = fon8x8;
    WORD i;
    char *c;

    (void)sfont_system_setting(name);           /* the file takes the place of the option */
    if (!name[0])
        return;
    for (c = name; *c; c++)                     /* GEMDOS names are upper case */
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 'a' + 'A');

    for (i = 0; i < vdi_sfont_count; i++)
        if (!strcmp(vdi_sfonts[i].file, name))
            break;
    if (i == vdi_sfont_count)
    {
        KINFO(("GDOS: the system font %s is not in %s; the ROM fonts stay\n", name, sfont_dir));
        return;
    }
    if (!(vdi_sfonts[i].flags & SFONT_MONOSPACE))
    {
        KINFO(("GDOS: the system font %s is not monospaced; the ROM fonts stay\n", name));
        return;
    }
    if (!country_charset_is_st())
    {
        KINFO(("GDOS: no scalable system font for this character set; the ROM fonts stay\n"));
        return;
    }

    /* both sizes or neither: rendered into copies of the headers, which replace the ROM's when both are done */
    if (vdi_sfont_make_system(i, &new16) && vdi_sfont_make_system(i, &new8))
    {
        fon8x16.off_table = new16.off_table;
        fon8x16.form_width = new16.form_width;
        fon8x16.dat_table = new16.dat_table;
        fon8x8.off_table = new8.off_table;
        fon8x8.form_width = new8.form_width;
        fon8x8.dat_table = new8.dat_table;
        font_forms_changed();
        KINFO(("GDOS: %s (%s) is the system font\n", vdi_sfonts[i].name, name));
    }
    else
    {
        if (new16.off_table != fon8x16.off_table)   /* the first was made, the second was not */
            vdi_ft_free((void *)new16.off_table);
        KINFO(("GDOS: the system font %s cannot be rendered; the ROM fonts stay\n", name));
    }
}

void vdi_sfont_rescan(void)
{
    if (vdi_gdos_id && !vdi_sfont_count && sfont_dir[0])
    {
        sfont_scan();
        KINFO(("GDOS: %d scalable fonts in %s now\n", vdi_sfont_count, sfont_dir));
    }
}


/*
 * Starting the built-in GDOS, from bios/bios.c once the AUTO folder has
 * run, so that a GDOS loaded from there takes precedence
 */
void vdi_gdos_init(void)
{
#if !ARCH_X86_64
    ULONG value;
#endif

    if (!vdi_ft_library)
        return;                     /* no FreeType, no scalable fonts */

#if !ARCH_X86_64
    if (!is_text_pointer((const void *)VEC_GEM)
        || cookie_get(COOKIE_FSMC, &value) || cookie_get(COOKIE_NVDI, &value))
    {
        KINFO(("GDOS: one was loaded from disk; the built-in one stays out\n"));
        return;
    }
#endif

#if !ARCH_X86_64
    /*
     * The cookie says which GDOS this is, so without it there is none:
     * cookie_add() does nothing when the AUTO folder has filled the jar.
     */
    fsmc.magic = 0x5f535044UL;      /* '_SPD' */
    fsmc.version = 0x0400;
    fsmc.quality = -1;
    cookie_add(COOKIE_FSMC, (ULONG)&fsmc);
    if (!cookie_get(COOKIE_FSMC, &value))
    {
        KINFO(("GDOS: no room in the cookie jar for FSMC; no built-in GDOS\n"));
        return;
    }
#endif

    sfont_set_dir();
    sfont_scan();
    sfont_system();
    vdi_gdos_id = GDOS_ID_FSM;

    KINFO(("GDOS: built in, %d scalable fonts in %s\n",
           vdi_sfont_count, sfont_dir));
}
