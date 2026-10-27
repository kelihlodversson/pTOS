/* Native XOR animations use the existing private renderer context. */
#include "render_bindings.h"
static void gr_setup(void)
{
    gsx_attr(FALSE,MD_XOR,BLACK);
}
