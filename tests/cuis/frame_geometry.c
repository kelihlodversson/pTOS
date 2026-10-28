/* Native edge cases, before wiring decorated wind_calc into public libaes. */
#include "gemframegeom.h"
#include "aesdefs.h"
#define VERIFY(c) do { if (!(c)) return __LINE__; } while (0)
int main(void)
{
    static const struct { UWORD kind; BOOL three_d; LONG top, right, bottom; } cases[] = {
        { 0,FALSE,1,1,1 }, { 0,TRUE,1,1,1 },
        { MOVER,FALSE,1,1,1 }, { MOVER,TRUE,23,1,1 },
        { NAME|INFO,FALSE,37,1,1 }, { NAME|INFO,TRUE,42,1,1 },
        { SIZER,FALSE,1,11,19 }, { SIZER,TRUE,1,15,1 },
        { UPARROW|SIZER,TRUE,1,15,1 }, { HSLIDE|SIZER,TRUE,1,1,23 },
        { VSLIDE|HSLIDE|SIZER,TRUE,1,15,23 }
    };
    WM_FRAME_INSETS r = { 123,456,789,1011 };
    unsigned i;
    VERIFY(!wm_frame_insets(0,0,19,FALSE,&r) && r.left == 123 && r.top == 456);
    VERIFY(!wm_frame_insets(0,11,-1,TRUE,&r) && r.right == 789 && r.bottom == 1011);
    VERIFY(!wm_frame_insets(0,11,19,TRUE,NULL));
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        VERIFY(wm_frame_insets(cases[i].kind,11,19,cases[i].three_d,&r));
        VERIFY(r.left == 1 && r.top == cases[i].top && r.right == cases[i].right && r.bottom == cases[i].bottom);
    }
    VERIFY(wm_frame_insets(NAME|INFO|SIZER,32767,32767,TRUE,&r));
    VERIFY(r.top == 65538 && r.right == 32771 && r.bottom == 1);
    return 0;
}
