/* Bind native tracking to the application's private pointer/painter state. */
#include "render_bindings.h"
#include "tracking.h"
#define GRTRACK_3D 1
#define xrat (aes_tracker->mouse[0])
#define yrat (aes_tracker->mouse[1])
#define gr_stilldn(out,x,y,w,h) aes_track_wait()
#define gr_setup aes_track_setup
#define wm_update(mode) ((void)0)
#define ob_actxywh aes_track_parent
#define ob_relxywh aes_track_child
static GRECT track_zero;
#define gl_rzero track_zero

static WORD track_mul_div_round(WORD a, WORD b, WORD divisor)
{ return ((LONG)a*b+divisor/2)/divisor; }
#define mul_div_round track_mul_div_round
