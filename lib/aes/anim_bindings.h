/* Native XOR animations use the existing private renderer context. */
#include "render_bindings.h"
static void gr_setup(WORD color)
{
    gsx_attr(FALSE,MD_XOR,color);
}
