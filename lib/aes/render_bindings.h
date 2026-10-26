/* Compile-time GSX bindings for separately compiled shared AES sources. */
#ifndef LIBAES_RENDER_BINDINGS_H
#define LIBAES_RENDER_BINDINGS_H
#include "render_context.h"
#define gl_handle (aes_painter->handle)
#define gl_ws (aes_painter->workstation)
#define gl_clip (aes_painter->clip)
#define gl_wchar (aes_painter->wchar)
#define gl_hchar (aes_painter->hchar)
#define gl_wschar (aes_painter->wschar)
#define gl_hschar (aes_painter->hschar)
#define gl_wbox (aes_painter->wbox)
#define gl_hbox (aes_painter->hbox)
#define gl_mode (aes_painter->mode)
#define gl_tcolor (aes_painter->text_color)
#define gl_lcolor (aes_painter->line_color)
#define gl_fis (aes_painter->interior)
#define gl_patt (aes_painter->pattern)
#define gl_font (aes_painter->font)
#define gl_wptschar (aes_painter->char_width)
#define gl_hptschar (aes_painter->char_top)
#define gl_wsptschar (aes_painter->small_width)
#define gl_hsptschar (aes_painter->small_top)
#define contrl (aes_painter->control)
#define intin (aes_painter->input)
#define ptsin (aes_painter->points)
#define intout (aes_painter->output)
#define ptsout (aes_painter->outpoints)
#define D (aes_painter->scratch)

#define OB_RENDER_3D 0
#define OB_RENDER_NICELINES 0
#define OB_RENDER_COLOUR_ICONS 0
#define OB_RENDER_EXTENDED 0
#endif
