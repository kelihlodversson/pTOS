/* Per-call GSX state, private to the userspace VDI adapter. */
#ifndef LIBAES_RENDER_CONTEXT_H
#define LIBAES_RENDER_CONTEXT_H
#include "emutos.h"
#include "gem.h"
#include "aesext.h"
#include "vdipb.h"
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
} PAINTER;

extern PAINTER *aes_painter;
CICON *aes_render_cicon(CICONBLK *block);
void aes_render_colourblit(const WORD *data, const GRECT *rect, WORD planes);
void *aes_render_alloc(LONG bytes);
WORD aes_render_free(void *memory);
void aes_edit_draw(OBJECT *tree, WORD object, WORD depth);
BOOL aes_render_ready(OBJECT *tree, WORD object, WORD x, WORD y);
BOOL aes_render_extent(const GRECT *rect, WORD state, WORD thickness);
void aes_render_border(LONG x, LONG y, LONG width, LONG height, WORD thickness);
void aes_render_bb_fill(WORD mode, WORD fis, WORD patt, LONG x, LONG y, LONG width, LONG height);
void aes_render_cline(LONG x1, LONG y1, LONG x2, LONG y2);
void aes_render_line(LONG x1, LONG y1, LONG x2, LONG y2);
void aes_render_box(LONG x, LONG y, LONG width, LONG height);
void aes_render_fill(LONG x, LONG y, LONG width, LONG height);

#endif
