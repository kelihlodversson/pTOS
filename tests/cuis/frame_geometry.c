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
    VERIFY(!wm_frame_insets(0,0,19,FALSE,640,400,&r) && r.left == 123 && r.top == 456);
    VERIFY(!wm_frame_insets(0,11,-1,TRUE,640,400,&r) && r.right == 789 && r.bottom == 1011);
    VERIFY(!wm_frame_insets(0,11,19,TRUE,640,400,NULL));
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        VERIFY(wm_frame_insets(cases[i].kind,11,19,cases[i].three_d,640,400,&r));
        VERIFY(r.left == 1 && r.top == cases[i].top && r.right == cases[i].right && r.bottom == cases[i].bottom);
    }
    {
        static const struct { WORD width, height; LONG right; } modes[] = {
            { 320,200,14 }, { 320,399,14 }, { 320,400,13 },
            { 639,400,13 }, { 640,200,15 }, { 640,400,15 }
        };
        for (i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
            VERIFY(wm_frame_insets(NAME|SIZER,11,19,TRUE,modes[i].width,modes[i].height,&r));
            VERIFY(r.left == 1 && r.top == 23 && r.right == modes[i].right && r.bottom == 1);
            VERIFY(wm_frame_insets(VSLIDE|HSLIDE,11,19,TRUE,modes[i].width,modes[i].height,&r));
            VERIFY(r.right == modes[i].right && r.bottom == 23);
            VERIFY(wm_frame_insets(SIZER|HSLIDE,11,19,TRUE,modes[i].width,modes[i].height,&r));
            VERIFY(r.right == 1 && r.bottom == 23);
            VERIFY(wm_frame_insets(NAME|SIZER,11,19,FALSE,modes[i].width,modes[i].height,&r));
            VERIFY(r.right == 11 && r.bottom == 19 && r.top == 19);
        }
        r.left = 123;
        VERIFY(!wm_frame_insets(SIZER,11,19,TRUE,0,400,&r) && r.left == 123);
        VERIFY(!wm_frame_insets(SIZER,11,19,TRUE,640,-1,&r) && r.left == 123);
    }
    VERIFY(wm_frame_insets(NAME|INFO|SIZER,32767,32767,TRUE,640,400,&r));
    VERIFY(r.top == 65538 && r.right == 32771 && r.bottom == 1);
    return 0;
}
