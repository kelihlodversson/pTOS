/* Internal interface shared by native AES and libaes drawing units. */
#ifndef GEMRENDER_H
#define GEMRENDER_H
#include "emutos.h"
#include "obdefs.h"

void ob_render_object(OBJECT *tree, WORD object, WORD x, WORD y);
void ob_render_change(OBJECT *tree, WORD object, UWORD state, WORD redraw);
BOOL ob_render_cancelled(OBJECT *tree);

/* Native GSX caches, initialised by gsx_start() and used by shared painting.
 * The userspace binding substitutes fields of its per-call context. */
extern WORD gl_mode, gl_tcolor, gl_lcolor, gl_fis, gl_patt, gl_font;
extern WORD gl_wptschar, gl_hptschar, gl_wsptschar, gl_hsptschar;

#endif
