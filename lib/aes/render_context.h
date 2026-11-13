/* Per-call GSX state, private to the userspace VDI adapter. */
#ifndef LIBAES_RENDER_CONTEXT_H
#define LIBAES_RENDER_CONTEXT_H
#include "emutos.h"
#include "libaes.h"
#include "aesext.h"
#include "vdipb.h"
#include "gsxdefs.h"
#include "style.h"
typedef struct painter {
    WORD handle, colour_planes;
    BOOL ok;
    TEDINFO edit;
    WS workstation;
    GRECT clip;
    WORD wchar, hchar, wschar, hschar, wbox, hbox;
    WORD mode, text_color, line_color, interior, pattern, font;
    WORD char_width, char_top, small_width, small_top;
    VDICONTROL control;
    WORD input[128], points[20], output[45], outpoints[12];
    struct { char g_rawstr[MAX_LEN], g_tmpstr[MAX_LEN], g_fmtstr[MAX_LEN], g_valstr[MAX_LEN]; } scratch;
    AES_3D_STYLE *style;
    AESGLOBAL *global;
    struct aes_track *tracker;
    GRECT zero;
} PAINTER;


void aes_init_3d(PAINTER *aes_painter);
WORD aes_sysvar_core(PAINTER *aes_painter, WORD mode, WORD which, WORD in1, WORD in2, WORD *out1, WORD *out2);
CICON *aes_render_cicon(PAINTER *aes_painter, CICONBLK *block);
void aes_render_colourblit(PAINTER *aes_painter, const WORD *data, const GRECT *rect, WORD planes);
void *aes_render_alloc(PAINTER *aes_painter, LONG bytes);
WORD aes_render_free(void *memory);
void aes_edit_rect(PAINTER *aes_painter, OBJECT *tree, WORD object, GRECT *rect);
void aes_edit_draw(PAINTER *aes_painter, OBJECT *tree, WORD object, WORD depth);
BOOL aes_render_ready(PAINTER *aes_painter, OBJECT *tree, WORD object, WORD x, WORD y);
BOOL aes_render_extent(PAINTER *aes_painter, const GRECT *rect, WORD state, WORD thickness);
void aes_render_border(PAINTER *aes_painter, LONG x, LONG y, LONG width, LONG height, WORD thickness);
void aes_render_bb_fill(PAINTER *aes_painter, WORD mode, WORD fis, WORD patt, LONG x, LONG y, LONG width, LONG height);
void aes_render_cline(PAINTER *aes_painter, LONG x1, LONG y1, LONG x2, LONG y2);
void aes_render_line(PAINTER *aes_painter, LONG x1, LONG y1, LONG x2, LONG y2);
void aes_render_box(PAINTER *aes_painter, LONG x, LONG y, LONG width, LONG height);
void aes_render_fill(PAINTER *aes_painter, LONG x, LONG y, LONG width, LONG height);

#endif
