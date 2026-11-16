/* Native XOR animations use the existing private renderer context. */
#include "render_bindings.h"
#include "animation.h"
#include "funcdef.h"
static void gr_setup(PAINTER *aes_painter)
{
    vsl_type(7);
    gsx_attr(aes_painter, FALSE,MD_XOR,BLACK);
}
