/*
 * vdi_backend.c - VDI drawing backend selection
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "vdi_defs.h"
#include "vdi_backend.h"
#include "../bios/tosvars.h"    /* v_bas_ad */
#include "../bios/lineavars.h"  /* linea_vars */
#include "asm.h"                /* rolw1/rorw1 */
#include "kprint.h"             /* KDEBUG */

const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode)
{
    if (!screen_mode_desc_valid(mode))
        return NULL;

    if (mode->layout == SCREEN_LAYOUT_PLANAR && mode->color_model == SCREEN_COLOR_INDEXED) {
        vdi_backend_ops_init(&planar_backend_ops);
        return &planar_backend_ops;
    }

#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    if (mode->layout == SCREEN_LAYOUT_PACKED
        && mode->color_model == SCREEN_COLOR_TRUECOLOR
        && mode->pixel_format == SCREEN_PIXEL_RGB565) {
        vdi_backend_ops_init(&packed_truecolor_backend_ops);
        return &packed_truecolor_backend_ops;
    }
#endif

    return NULL;
}

/*
 * Generic backend defaults (issue #138): renderer-agnostic fallbacks built
 * only on the mandatory primitives (get_start_addr, get_pixel, put_pixel,
 * get_raw_pixel, put_raw_pixel).  They run through vdi_screen_backend(),
 * which always returns the backend whose table these defaults are installed
 * in -- only the selected screen backend's table is ever dispatched through.
 */
static BOOL default_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static void default_close(Vwk *vwk)
{
    (void)vwk;
}

static void default_fill_rect(const VwkAttrib *attr, const Rect *rect)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    const UWORD patmsk = attr->patmsk;
    WORD x, y, i;

    for (y = rect->y1; y <= rect->y2; y++) {
        UWORD pattern = attr->patptr[patmsk & y];

        for (x = rect->x1, i = 0; x <= rect->x2; x++, i++) {
            BOOL set = (pattern & (0x8000 >> (i & 15))) != 0;

            switch (attr->wrt_mode) {
            case 3:                 /* erase (reverse transparent) */
                if (!set)
                    ops->put_pixel(x, y, attr->color);
                break;
            case 2:                 /* xor -- invert the raw word (palette
                                     * indices cannot express bitwise ops) */
                if (set)
                    ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff);
                break;
            case 1:                 /* transparent */
                if (set)
                    ops->put_pixel(x, y, attr->color);
                break;
            default:                /* replace -- unset bits paint index 0 */
                ops->put_pixel(x, y, set ? attr->color : 0);
                break;
            }
        }
    }
}

/*
 * fetch the source word in big-endian (Motorola font) byte order -- the
 * same helper the truecolor text blit uses (see vdi_backend_truecolor.c).
 */
static UWORD get_src_word(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | (UWORD)p[1]);
}

static void default_text_blit(LOCALVARS *vars)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    UBYTE *src, *p;
    UWORD src_mask, mask, src_word;
    WORD w, h, x, y;

    /*
     * No skew support here (skewed text needs a backend-provided
     * text_blit); WM_XOR is handled through the raw pixel pair.
     */
    src = vars->sform;
    src_mask = 0x8000 >> vars->tsdad;
    y = vars->DESTY + vars->DELY - 1;   /* we draw from the bottom up */

    for (h = vars->height; h > 0; h--, src += vars->s_next, y--) {
        p = src;
        x = vars->DESTX;
        mask = src_mask;

        for (w = vars->width; w > 0; w--) {
            src_word = get_src_word(p);

            switch (vars->WRT_MODE) {
            case WM_REPLACE:
                ops->put_pixel(x, y, (src_word & mask) ? (UWORD)vars->forecol : 0);
                break;
            case WM_TRANS:
                if (src_word & mask)
                    ops->put_pixel(x, y, (UWORD)vars->forecol);
                break;
            case WM_ERASE:
                if (!(src_word & mask))
                    ops->put_pixel(x, y, (UWORD)vars->forecol);
                break;
            case WM_XOR:
                if (src_word & mask)
                    ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff);
                break;
            }
            x++;
            rorw1(mask);
            if (mask == 0x8000)
                p += 2;
        }
    }
}

/*
 * apply a VDI boolean raster-op (see BM_* in vdi_raster.h) to a raw source
 * and destination pixel -- the same semantics as the per-plane blitter and
 * the truecolor backend's word-at-a-time copy.
 */
static UWORD apply_raster_op(WORD op, UWORD src, UWORD dst)
{
    switch (op & 0x0f) {
    case BM_ALL_WHITE:  return 0x0000;
    case BM_S_AND_D:    return (UWORD)(src & dst);
    case BM_S_AND_NOTD: return (UWORD)(src & ~dst);
    case BM_S_ONLY:     return src;
    case BM_NOTS_AND_D: return (UWORD)(~src & dst);
    case BM_D_ONLY:     return dst;
    case BM_S_XOR_D:    return (UWORD)(src ^ dst);
    case BM_S_OR_D:     return (UWORD)(src | dst);
    case BM_NOT_SORD:   return (UWORD)~(src | dst);
    case BM_NOT_SXORD:  return (UWORD)~(src ^ dst);
    case BM_NOT_D:      return (UWORD)~dst;
    case BM_S_OR_NOTD:  return (UWORD)(src | ~dst);
    case BM_NOT_S:      return (UWORD)~src;
    case BM_NOTS_OR_D:  return (UWORD)(~src | dst);
    case BM_NOT_SANDD:  return (UWORD)~(src & dst);
    case BM_ALL_BLACK:  return 0xffff;
    default:            return dst;
    }
}

static void default_raster_copy(struct raster_t *raster, struct blit_frame *info)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    WORD y;

    /*
     * The default operates on the screen through the ops primitives, so a
     * destination (or opaque source) that is not the screen framebuffer has
     * no generic fallback -- a backend that needs it implements raster_copy.
     */
    if (info->d_form != (UWORD *)v_bas_ad)
        return;

    if (raster->transparent) {
        /* 1bpp icon source to packed screen; fg/bg are palette indices */
        for (y = 0; y < info->b_ht; y++) {
            const UBYTE *srow = (const UBYTE *)info->s_form
                + (LONG)(info->s_ymin + y) * info->s_nxln;
            const UBYTE *p = srow + (LONG)(info->s_xmin >> 4) * info->s_nxwd;
            UWORD mask = 0x8000 >> (info->s_xmin & 0x0f);
            WORD x;
            const WORD dy = info->d_ymin + y;

            for (x = 0; x < info->b_wd; x++) {
                BOOL set = (*(const UWORD *)p & mask) != 0;
                const WORD dx = info->d_xmin + x;

                switch (raster->mode) {
                case MD_REPLACE:
                    ops->put_pixel(dx, dy, set ? raster->fg_col : raster->bg_col);
                    break;
                case MD_TRANS:
                    if (set)
                        ops->put_pixel(dx, dy, raster->fg_col);
                    break;
                case MD_XOR:
                    if (set)
                        ops->put_raw_pixel(dx, dy, ops->get_raw_pixel(dx, dy) ^ 0xffff);
                    break;
                case MD_ERASE:
                    if (!set)
                        ops->put_pixel(dx, dy, raster->bg_col);
                    break;
                }
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
        }
        return;
    }

    /* opaque: packed screen to packed screen via the raw pixel pair */
    if (info->s_form != (UWORD *)v_bas_ad)
        return;

    {
        BOOL forward_y = TRUE, forward_x = TRUE;

        /* never overwrite source pixels before they're read */
        if (info->d_ymin > info->s_ymin)
            forward_y = FALSE;
        else if ((info->d_ymin == info->s_ymin) && (info->d_xmin > info->s_xmin))
            forward_x = FALSE;

        for (y = 0; y < info->b_ht; y++) {
            WORD row = forward_y ? y : (info->b_ht - 1 - y);
            WORD sy = info->s_ymin + row;
            WORD dy = info->d_ymin + row;
            WORD x;

            for (x = 0; x < info->b_wd; x++) {
                WORD col = forward_x ? x : (info->b_wd - 1 - x);
                WORD sx = info->s_xmin + col;
                WORD dx = info->d_xmin + col;

                ops->put_raw_pixel(dx, dy, apply_raster_op(info->op_tab[0],
                    ops->get_raw_pixel(sx, sy), ops->get_raw_pixel(dx, dy)));
            }
        }
    }
}

static UWORD default_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    WORD x, y, dx, dy, sx, sy, loopcnt;

    if (line->x2 < line->x1) {
        x = line->x2; y = line->y2;
        dx = line->x1 - line->x2;
        dy = line->y1 - line->y2;
    } else {
        x = line->x1; y = line->y1;
        dx = line->x2 - line->x1;
        dy = line->y2 - line->y1;
    }
    if (dy < 0) {
        dy = -dy;
        sy = -1;
    } else {
        sy = 1;
    }
    sx = 1;

    if (dx >= dy) {
        WORD eps = -dx, e1 = 2 * dy, e2 = 2 * dx;

        for (loopcnt = dx; loopcnt >= 0; loopcnt--) {
            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) ops->put_pixel(x, y, (UWORD)(~color & 0xff)); break;
            case 2: if (linemask & 1) ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff); break;
            case 1: if (linemask & 1) ops->put_pixel(x, y, color); break;
            default: ops->put_pixel(x, y, (linemask & 1) ? color : 0); break;
            }
            x += sx;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                y += sy;
            }
        }
    } else {
        WORD eps = -dy, e1 = 2 * dx, e2 = 2 * dy;

        for (loopcnt = dy; loopcnt >= 0; loopcnt--) {
            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) ops->put_pixel(x, y, (UWORD)(~color & 0xff)); break;
            case 2: if (linemask & 1) ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff); break;
            case 1: if (linemask & 1) ops->put_pixel(x, y, color); break;
            default: ops->put_pixel(x, y, (linemask & 1) ? color : 0); break;
            }
            y += sy;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                x += sx;
            }
        }
    }

    return linemask;
}

static WORD default_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    const vdi_backend_ops *ops = vdi_screen_backend();

    while (x++ < clip->xmx_clip) {
        if (ops->get_pixel(x, y) != search_col)
            break;
    }
    return x - 1;       /* output x coord -1 to endxright. */
}

static WORD default_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    const vdi_backend_ops *ops = vdi_screen_backend();

    while (x-- > clip->xmn_clip) {
        if (ops->get_pixel(x, y) != search_col)
            break;
    }
    return x + 1;       /* output x coord + 1 to endxleft. */
}

void vdi_backend_ops_init(vdi_backend_ops *ops)
{
    if (!ops->open) ops->open = default_open;
    if (!ops->close) ops->close = default_close;
    if (!ops->fill_rect) ops->fill_rect = default_fill_rect;
    if (!ops->text_blit) ops->text_blit = default_text_blit;
    if (!ops->raster_copy) ops->raster_copy = default_raster_copy;
    if (!ops->draw_line) ops->draw_line = default_draw_line;
    if (!ops->search_right) ops->search_right = default_search_right;
    if (!ops->search_left) ops->search_left = default_search_left;

    if (!ops->get_start_addr || !ops->get_pixel || !ops->put_pixel
        || !ops->get_raw_pixel || !ops->put_raw_pixel)
        KDEBUG(("vdi_backend_ops_init: backend is missing a mandatory primitive\n"));
}
