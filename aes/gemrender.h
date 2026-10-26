/* Internal interface shared by native AES and libaes drawing units. */
#ifndef GEMRENDER_H
#define GEMRENDER_H
#include "emutos.h"
#include "obdefs.h"

void ob_render_object(OBJECT *tree, WORD object, WORD x, WORD y);
void ob_render_change(OBJECT *tree, WORD object, UWORD state, WORD redraw);
BOOL ob_render_cancelled(OBJECT *tree);

/* The library build supplies a private drawing context instead of AES globals.
 * Include this header after the declarations used by the renderer. */
#ifdef AES_RENDER_BINDINGS
#include AES_RENDER_BINDINGS
#endif
#ifndef OB_RENDER_3D
#define OB_RENDER_3D CONF_WITH_3D_OBJECTS
#define OB_RENDER_NICELINES CONF_WITH_NICELINES
#define OB_RENDER_COLOUR_ICONS CONF_WITH_COLOUR_ICONS
#define OB_RENDER_EXTENDED CONF_WITH_EXTENDED_OBJECTS
#endif
#endif
