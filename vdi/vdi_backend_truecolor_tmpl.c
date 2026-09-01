/*
 * vdi_backend_truecolor_tmpl.c - shared packed-truecolor drawing code
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * This is NOT a build target: it is #included by the two truecolor
 * backend wrappers, vdi_backend_truecolor.c (RGB565, 16bpp) and
 * vdi_backend_truecolor32.c (XRGB8888, 32bpp), which compile it into
 * their own TU.  It must therefore define nothing but `static`
 * functions, and it requires exactly two macros to be defined by the
 * including wrapper before the #include:
 *
 *   PIXEL       the packed pixel type: UWORD for 16bpp, ULONG for 32bpp
 *   PIXEL_SIZE  bytes per packed pixel: 2 or 4
 *
 * The two wrappers undef both macros afterwards.  Shared state (the
 * default palette, the active-workstation bookkeeping and the format-
 * independent exports) lives in vdi_backend_truecolor.c, which is
 * always built; the 32bpp wrapper holds none of it.
 *
 * Colour conversion is deliberately absent here: every routine maps a
 * hardware palette index through tc_pixel_for_index() below, which reads
 * the (widened) tc_palette[] that the wrapper seeds with the active
 * format's packed value -- so this code is format-agnostic.
 */

#if CONF_VDI_SPARSE_TABLE
/* Unreferenced in sparse builds, where the wrapper's ops table leaves the
 * optional slots NULL (see vdi_backend_truecolor.c).  The two helpers
 * below (get_src_word, apply_raster_op) are only called by the optional-slot
 * functions, so they become unreferenced there too. */
#define TC_SPARSE_UNUSED __attribute__((unused))
#else
#define TC_SPARSE_UNUSED
#endif

/*
 * Turn a MAP_COL-mapped hardware palette register index into the active
 * format's packed pixel value.  Reads the per-workstation palette of
 * vdi_backend_active_vwk() (see vdi_backend.h) like the functions that
 * take no Vwk* below.
 */
static PIXEL tc_pixel_for_index(WORD index)
{
    if (index < 0 || index > 255)
        index = 0;

    return (PIXEL)vdi_backend_active_vwk()->tc_palette[index];
}

/*
 * Address calculation for a packed framebuffer (PIXEL_SIZE bytes/pixel).
 * PIXEL_SIZE is fixed per instantiation, matching the mode descriptor
 * this backend was selected for (SCREEN_PIXEL_RGB565/XRGB8888).
 */
static PIXEL *tc_get_start_addr(WORD x, WORD y)
{
    UBYTE *addr;

    addr = v_bas_ad;
    addr += (LONG)x * PIXEL_SIZE;
    addr += (LONG)y * linea_vars.v_lin_wr;
    return (PIXEL *)addr;
}

static UWORD tc_get_pixel(WORD x, WORD y)
{
    PIXEL raw = *tc_get_start_addr(x, y);
    const ULONG *palette = vdi_backend_active_vwk()->tc_palette;
    WORD i;

    /*
     * Callers expect a hardware palette register index back (see the
     * comment on default_prgb_palette[] above), not a 0-15 VDI pen
     * number, so this has to search the full 256-entry space to match.
     * The stored palette values are ULONGs now (the active format's
     * packed pixel); for RGB565 the upper bits are zero.
     */
    for (i = 0; i < 256; i++) {
        if (palette[i] == (ULONG)raw)
            return (UWORD)i;
    }

    return 0;   /* not one of the active palette's 256 -- index 0 is white, the closest we can do without guessing */
}

static void tc_put_pixel(WORD x, WORD y, UWORD color)
{
    PIXEL *addr = tc_get_start_addr(x, y);

    *addr = tc_pixel_for_index((WORD)color);
}

static void TC_SPARSE_UNUSED tc_fill_rect(const VwkAttrib *attr, const Rect *rect)
{
    const UWORD patmsk = attr->patmsk;
    PIXEL pixel = tc_pixel_for_index((WORD)attr->color);
    UBYTE *row = (UBYTE *)tc_get_start_addr(0, rect->y1);
    WORD x, y, i;

    for (y = rect->y1; y <= rect->y2; y++, row += linea_vars.v_lin_wr) {
        WORD patind = patmsk & y;   /* starting pattern */
        UWORD pattern = attr->patptr[patind];
        PIXEL *dst = (PIXEL *)row;

        for (x = rect->x1, i = 0; x <= rect->x2; x++, i++) {
            BOOL set = (pattern & (0x8000U >> (i & 15))) != 0;

            switch (attr->wrt_mode) {
            case 3:                 /* erase (reverse transparent) mode */
                if (!set)
                    dst[x] = pixel;
                break;
            case 2:                 /* xor mode */
                /*
                 * The planar path XORs the pattern into every plane
                 * unconditionally -- a full bitwise invert, independent
                 * of attr->color (this is how the AES draws rubber-band
                 * selection boxes). XOR-ing in the mapped foreground
                 * pixel instead is not equivalent: it's a no-op for any
                 * pen mapping to 0x0000, and produces an arbitrary
                 * colour rather than an inversion for anything else. So
                 * this ignores attr->color/pixel entirely and inverts,
                 * matching planar's actual semantics.
                 */
                if (set)
                    dst[x] ^= (PIXEL)-1;
                break;
            case 1:                 /* transparent mode */
                if (set)
                    dst[x] = pixel;
                break;
            default:                /* replace mode */
                /*
                 * Unset pattern bits paint pen 0 (white by default),
                 * matching the planar path, which writes color index 0
                 * for unset bits -- not raw RGB565 0 (black).
                 */
                dst[x] = set ? pixel : tc_pixel_for_index(0);
            }
        }
    }
}

/*
 * fetch the source word in big-endian (Motorola font) byte order
 *
 * The text source -- the font itself, or the intermediate buffer that
 * pre_blit() filled -- is a big-endian byte stream: normal_blit() in
 * vdi/arch/arm/vdi_tblit.c reads it as a sequence of bytes for that
 * reason, and the m68k assembler reads it as big-endian words.  A native
 * UWORD load byte-swaps adjacent glyphs on little-endian machines (even
 * character codes render their successor, odd ones their predecessor),
 * so assemble the word from its bytes instead.
 */
static UWORD TC_SPARSE_UNUSED get_src_word(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | (UWORD)p[1]);
}

/*
 * truecolor text blit: output the current glyph to a packed truecolor
 * screen (RGB565 or XRGB8888, per the instantiation)
 *
 * port of the upstream screen_blit16() (see vdi_textblit.c in EmuTOS),
 * adapted to the pTOS backend contract: colours come from the backend's
 * own palette conversion instead of CUR_WORK->ext->palette, and line-A
 * variables are read through linea_vars.
 */
static void TC_SPARSE_UNUSED tc_text_blit(LOCALVARS *vars)
{
    UBYTE *p;
    PIXEL *q;
    UBYTE *src, *dst;
    PIXEL fgcol, bgcol;
    UWORD src_mask, mask, skew_mask;
    WORD h, w, skew, skew_start;

    /*
     * set skew-related values
     *
     * NOTE: we can't test for skewed text using vars->STYLE, since
     * pre_blit() clears F_SKEW and F_THICKEN after it has processed them.
     */
    skew = linea_vars.LOFF + linea_vars.ROFF;
    skew_mask = (UWORD)vars->skew_msk;
    skew_start = vars->height;

    /*
     * the following adjustments are for skewed+outlined text, and make
     * the output almost the same as produced by TOS4.
     *
     * 1. since the source of skewed and/or outlined text must be an
     *    intermediate buffer, SOURCEX *must* be 0, and we force that.
     *    NOTE: in versions of TOS prior to TOS4 (& in TOS4 non-TC
     *    resolutions), this adjustment is not made.  As a result, text
     *    output is typically clipped.
     *
     * 2. a negative value for the nominal destination position is OK,
     *    because outlining has adjusted the starting position of characters
     *    leftwards.  however, such values are prohibited by do_clip(),
     *    which adjusts var->DESTX.  we adjust it back here ...
     *    NOTE: this situation can only happen at the beginning of a
     *    screen line.
     *
     * 3. for bigger fonts, skewing must not start at the bottom of the
     *    buffer, otherwise parts of the outline are clipped too agressively.
     *    at the moment, this fix is a bit of a kludge, though it works well
     *    enough.
     */
    if (skew && (vars->STYLE&F_OUTLINE))
    {
        if (linea_vars.SOURCEX)
        {
            KDEBUG(("SOURCEX (was %d) forced to zero for intermediate buffer\n", linea_vars.SOURCEX));
            linea_vars.SOURCEX = 0;
            vars->tsdad = 0;    /* this was set from SOURCEX in screen_blit() */
        }

        if (linea_vars.DESTX < 0)
        {
            KDEBUG(("vars->DESTX (was %d) set to DESTX (%d)\n", vars->DESTX, linea_vars.DESTX));
            vars->DESTX = linea_vars.DESTX;
        }
        if (vars->height > 8)       /* not a 6-point font */
            skew_start -= OUTLINE_THICKNESS;
    }

    /*
     * set up source stuff
     */
    src = vars->sform;
    src_mask = 0x8000 >> vars->tsdad;

    /*
     * set up destination stuff
     */
    vars->dform = v_bas_ad;
    vars->dform += vars->DESTX * PIXEL_SIZE;    /* add x coordinate part of addr */
    vars->dform += (UWORD)(vars->DESTY+vars->DELY-1) * (ULONG)linea_vars.v_lin_wr; /* add y coordinate part of addr */
    vars->d_next = -linea_vars.v_lin_wr;
    dst = vars->dform;

    /*
     * set up colours
     */
    fgcol = tc_pixel_for_index(vars->forecol);
    bgcol = tc_pixel_for_index(0);

    switch(vars->WRT_MODE) {
    /*
     * when called via lineA, modes 4-19 (corresponding to BitBlt modes 0-15)
     * are theoretically possible.  however, at this time we do not support them.
     */
    default:    /* WM_REPLACE */
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (PIXEL *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                *q++ = (get_src_word(p) & mask) ? fgcol : bgcol;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * special handling for skewed text: since the character cells
             * are effectively slanted, we must shift the starting position
             * of a cell rightwards as we go up the character.
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += PIXEL_SIZE;
                }
            }
        }
        break;
    case WM_TRANS:
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (PIXEL *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                if (get_src_word(p) & mask)
                    *q = fgcol;
                q++;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * see comments for WM_REPLACE (above) for an explanation of
             * the following
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += PIXEL_SIZE;
                }
            }
        }
        break;
    case WM_XOR:
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (PIXEL *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                if (get_src_word(p) & mask)
                    *q = (PIXEL)~*q;
                q++;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * see comments for WM_REPLACE (above) for an explanation of
             * the following
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += PIXEL_SIZE;
                }
            }
        }
        break;
    case WM_ERASE:
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (PIXEL *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                /*
                 * behaviour here differs from TOS 4.04 - for further info,
                 * see the comments in direct_screen_blit16()
                 */
                if (!(get_src_word(p) & mask))
                    *q = fgcol;
                q++;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * see comments for WM_REPLACE (above) for an explanation of
             * the following
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += PIXEL_SIZE;
                }
            }
        }
        break;
    }
}

/*
 * apply a VDI boolean raster-op (see BM_* in vdi_raster.h) to a source
 * and destination pixel, a whole packed pixel at a time -- the same
 * semantics the planar blitter emulator's do_blit() applies per
 * bitplane in vdi_raster.c, just applied once per pixel since this
 * backend has no planes to loop over.
 */
static PIXEL TC_SPARSE_UNUSED apply_raster_op(WORD op, PIXEL src, PIXEL dst)
{
    switch (op & 0x0f) {
    case BM_ALL_WHITE:  return (PIXEL)0;
    case BM_S_AND_D:    return (PIXEL)(src & dst);
    case BM_S_AND_NOTD: return (PIXEL)(src & ~dst);
    case BM_S_ONLY:     return src;
    case BM_NOTS_AND_D: return (PIXEL)(~src & dst);
    case BM_D_ONLY:     return dst;
    case BM_S_XOR_D:    return (PIXEL)(src ^ dst);
    case BM_S_OR_D:     return (PIXEL)(src | dst);
    case BM_NOT_SORD:   return (PIXEL)~(src | dst);
    case BM_NOT_SXORD:  return (PIXEL)~(src ^ dst);
    case BM_NOT_D:      return (PIXEL)~dst;
    case BM_S_OR_NOTD:  return (PIXEL)(src | ~dst);
    case BM_NOT_S:      return (PIXEL)~src;
    case BM_NOTS_OR_D:  return (PIXEL)(~src | dst);
    case BM_NOT_SANDD:  return (PIXEL)~(src & dst);
    case BM_ALL_BLACK:  return (PIXEL)-1;
    default:            return dst;
    }
}

/*
 * truecolor raster copy: backs vro_cpyfm()/vrt_cpyfm()/linea_raster()
 * (see cpy_raster() in vdi_raster.c) for the packed truecolor screen.
 *
 * setup_info() only ever reports s_nxwd/d_nxwd == PIXEL_SIZE and
 * plane_ct == 1 for a screen-side MFDB with this backend selected -- a
 * screen word is already one whole pixel, there are no bitplanes to
 * interleave.  Anything else (a multi-plane colour-icon MFDB, see
 * gr_colourblit() in aes/gemgraf.c) falls outside what this backend can
 * interpret; rather than misreading plane-interleaved memory as packed
 * pixels, it is silently skipped -- colour icons don't render via this
 * path yet, which is no worse than the memory corruption the planar
 * blitter emulator would otherwise produce here.
 */
static void TC_SPARSE_UNUSED tc_raster_copy(struct raster_t *raster, struct blit_frame *info)
{
    WORD y;

    if (info->d_nxwd != PIXEL_SIZE)
        return;

    if (raster->transparent) {
        /*
         * 1bpp source (an icon shape/mask) to packed colour destination.
         * fg_col/bg_col are hardware palette indices; raster->mode is the
         * write mode requested by INTIN[0] (MD_REPLACE/TRANS/XOR/ERASE)
         * -- see the switch in cpy_raster() this mirrors, and
         * tc_text_blit() above for the same source-bit-walking idiom
         * applied to glyphs instead of icons.
         */
        PIXEL fgpix = tc_pixel_for_index((WORD)raster->fg_col);
        PIXEL bgpix = tc_pixel_for_index((WORD)raster->bg_col);

        for (y = 0; y < info->b_ht; y++) {
            const UBYTE *srow = (const UBYTE *)info->s_form
                + (LONG)(info->s_ymin + y) * info->s_nxln;
            UBYTE *drow = (UBYTE *)info->d_form
                + (LONG)(info->d_ymin + y) * info->d_nxln;
            const UBYTE *p = srow + (LONG)(info->s_xmin >> 4) * info->s_nxwd;
            PIXEL *q = (PIXEL *)(drow + (LONG)info->d_xmin * info->d_nxwd);
            UWORD mask = 0x8000 >> (info->s_xmin & 0x0f);
            WORD x;

            for (x = 0; x < info->b_wd; x++) {
                /*
                 * Icon mask/data words (unlike font glyph bytes -- see
                 * get_src_word() above) are stored as WORD *value*
                 * arrays by the resource compiler (tools/erd.c), which
                 * the target compiler already lays out in its native
                 * byte order. A native dereference here matches that,
                 * and matches how the planar blitter reads the same
                 * MFDB-sourced words (GetMemW() in vdi_raster.c);
                 * get_src_word()'s manual big-endian byte reassembly
                 * would double-handle the byte order and scramble every
                 * word on a little-endian target.
                 */
                BOOL set = (*(const UWORD *)p & mask) != 0;

                switch (raster->mode) {
                case MD_REPLACE:
                    *q = set ? fgpix : bgpix;
                    break;
                case MD_TRANS:
                    if (set)
                        *q = fgpix;
                    break;
                case MD_XOR:
                    if (set)
                        *q = (PIXEL)~*q;
                    break;
                case MD_ERASE:
                    if (!set)
                        *q = bgpix;
                    break;
                }
                q++;

                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
        }
        return;
    }

    /* COPY RASTER OPAQUE: packed destination pixel == packed source pixel */
    if (info->s_nxwd != PIXEL_SIZE)
        return;

    {
        BOOL forward_y = TRUE, forward_x = TRUE;

        /*
         * Source and destination can be the same screen buffer (e.g. a
         * window drag or scroll) with overlapping rectangles -- pick a
         * scan direction that never overwrites source pixels before
         * they've been read, the same way bit_blt() picks a starting
         * corner for the planar blitter.
         */
        if (info->s_form == info->d_form) {
            if (info->d_ymin > info->s_ymin)
                forward_y = FALSE;
            else if ((info->d_ymin == info->s_ymin) && (info->d_xmin > info->s_xmin))
                forward_x = FALSE;
        }

        for (y = 0; y < info->b_ht; y++) {
            WORD row = forward_y ? y : (info->b_ht - 1 - y);
            const PIXEL *srow = (const PIXEL *)((const UBYTE *)info->s_form
                + (LONG)(info->s_ymin + row) * info->s_nxln + (LONG)info->s_xmin * info->s_nxwd);
            PIXEL *drow = (PIXEL *)((UBYTE *)info->d_form
                + (LONG)(info->d_ymin + row) * info->d_nxln + (LONG)info->d_xmin * info->d_nxwd);
            WORD x;

            for (x = 0; x < info->b_wd; x++) {
                WORD col = forward_x ? x : (info->b_wd - 1 - x);
                drow[col] = apply_raster_op(info->op_tab[0], srow[col], drow[col]);
            }
        }
    }
}

/*
 * truecolor draw_line: backs abline() (vdi_line.c) for non-horizontal
 * lines on the packed truecolor screen (abline() handles horizontal
 * lines itself, via fill_rect(), and never calls this for one).
 *
 * A packed screen has no bitplanes to loop over, so this is a single-pass
 * Bresenham writing one whole pixel per step, unlike the planar
 * implementation's per-bitplane loop (planar_draw_line() in vdi_line.c).
 * The write-mode semantics are derived directly from that function's
 * per-bitplane logic, composed across a whole pixel instead of one bit
 * per plane:
 *  - replace: every step writes either the line color or (at a line-
 *    style gap) palette index 0 -- matching replace mode's planar
 *    behaviour of also clearing gaps, not leaving them untouched.
 *  - transparent (or): only "on" steps write the line color; gaps are
 *    untouched.
 *  - xor: only "on" steps invert whatever pixel is already there,
 *    regardless of the requested color -- matching the planar loop's
 *    unconditional bit flip (compare tc_fill_rect()'s xor case).
 *  - reverse transparent (not): only "on" steps write the complement of
 *    the line color's palette index, mapped through the palette; gaps
 *    are untouched.
 */
static UWORD TC_SPARSE_UNUSED tc_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask)
{
    UWORD x1, y1, x2, y2;
    WORD dx, dy, loopcnt;
    LONG yinc;
    UBYTE *adr;
    PIXEL fgpix, bg0pix, notpix;

    if (line->x2 < line->x1) {
        x1 = line->x2; y1 = line->y2;
        x2 = line->x1; y2 = line->y1;
    } else {
        x1 = line->x1; y1 = line->y1;
        x2 = line->x2; y2 = line->y2;
    }

    dx = x2 - x1;
    dy = y2 - y1;

    fgpix = tc_pixel_for_index((WORD)color);
    bg0pix = tc_pixel_for_index(0);
    notpix = tc_pixel_for_index((WORD)(~color & 0xff));

    if (dy < 0) {
        dy = -dy;
        yinc = -(LONG)linea_vars.v_lin_wr;
    } else {
        yinc = (LONG)linea_vars.v_lin_wr;
    }
    adr = (UBYTE *)tc_get_start_addr(x1, y1);

    if (dx >= dy) {
        WORD eps = -dx, e1 = 2*dy, e2 = 2*dx;

        for (loopcnt = dx; loopcnt >= 0; loopcnt--) {
            PIXEL *p = (PIXEL *)adr;

            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) *p = notpix; break;
            case 2: if (linemask & 1) *p ^= (PIXEL)-1; break;
            case 1: if (linemask & 1) *p = fgpix; break;
            default: *p = (linemask & 1) ? fgpix : bg0pix; break;
            }
            adr += PIXEL_SIZE;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                adr += yinc;
            }
        }
    } else {
        WORD eps = -dy, e1 = 2*dx, e2 = 2*dy;

        for (loopcnt = dy; loopcnt >= 0; loopcnt--) {
            PIXEL *p = (PIXEL *)adr;

            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) *p = notpix; break;
            case 2: if (linemask & 1) *p ^= (PIXEL)-1; break;
            case 1: if (linemask & 1) *p = fgpix; break;
            default: *p = (linemask & 1) ? fgpix : bg0pix; break;
            }
            adr += yinc;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                adr += PIXEL_SIZE;
            }
        }
    }

    return linemask;
}

/*
 * truecolor_search_right/truecolor_search_left: scan a horizontal run of
 * matching color on the packed truecolor screen, for contourfill()'s
 * seed-fill (see end_pts() in vdi_fill.c and the vdi_backend_ops comment
 * in vdi_backend.h).
 *
 * search_col is a MAP_COL-mapped hardware palette index, like
 * get_pixel()'s return value -- converted to its raw packed pixel once,
 * up front, rather than calling get_pixel() (and paying its 256-entry
 * reverse palette search) again for every pixel of a scan that can span
 * the whole screen width.
 */
static WORD TC_SPARSE_UNUSED tc_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    PIXEL pixel = tc_pixel_for_index((WORD)search_col);
    const PIXEL *addr = tc_get_start_addr(x, y);

    while (x++ < clip->xmx_clip) {
        if (*++addr != pixel)
            break;
    }
    return x - 1;       /* output x coord -1 to endxright. */
}

static WORD TC_SPARSE_UNUSED tc_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    PIXEL pixel = tc_pixel_for_index((WORD)search_col);
    const PIXEL *addr = tc_get_start_addr(x, y);

    while (x-- > clip->xmn_clip) {
        if (*--addr != pixel)
            break;
    }
    return x + 1;       /* output x coord + 1 to endxleft. */
}

static ULONG tc_get_raw_pixel(WORD x, WORD y)
{
    return (ULONG)*tc_get_start_addr(x, y);
}

static void tc_put_raw_pixel(WORD x, WORD y, ULONG raw)
{
    *tc_get_start_addr(x, y) = (PIXEL)raw;
}
