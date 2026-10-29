/* Rubber-window minima retain native metrics and admit drawable frames. */
#include "gemsizegeom.h"
#include "gemframegeom.h"
#include "aesdefs.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int main(void)
{
    GRECT start = { 20,30,200,140 }, result = { 99,99,99,99 };
    WM_FRAME_INSETS insets;
    UWORD kinds[] = { SIZER,NAME|SIZER,NAME|INFO|CLOSER|FULLER|MOVER|SIZER,
        SIZER|UPARROW|DNARROW|VSLIDE,SIZER|LFARROW|RTARROW|HSLIDE };
    unsigned k;
    LONG x,y;
    CHECK(wm_size_position(&start,NAME|SIZER,260,200,8,16,19,19,640,400,&result));
    CHECK(result.g_x == 20 && result.g_y == 30 && result.g_w == 241 && result.g_h == 171);
    CHECK(wm_size_position(&start,NAME|SIZER,-100,-100,8,16,19,19,640,400,&result));
    CHECK(result.g_w == 76 && result.g_h == 44);
    CHECK(wm_size_position(&start,NAME|INFO|SIZER,-100,-100,8,16,19,19,640,400,&result));
    CHECK(result.g_w == 76 && result.g_h == 63);
    CHECK(wm_size_position(&start,SIZER,-100,-100,8,16,19,19,640,400,&result));
    CHECK(result.g_w == 25 && result.g_h == 21);
    CHECK(wm_size_position(&start,SIZER,-100,-100,8,16,19,19,320,200,&result));
    CHECK(result.g_w == 24 && result.g_h == 21);
    CHECK(wm_size_position(&start,NAME|SIZER,-100,-100,8,16,38,19,640,400,&result));
    CHECK(result.g_w == 152 && result.g_h == 44);
    for (k = 0; k < sizeof(kinds)/sizeof(kinds[0]); k++) {
        CHECK(wm_frame_insets(kinds[k],19,19,TRUE,640,400,&insets));
        for (x = -100; x < 1000; x += 113) for (y = -100; y < 1000; y += 97) {
            CHECK(wm_size_position(&start,kinds[k],x,y,8,16,19,19,640,400,&result));
            CHECK(result.g_x == 20 && result.g_y == 30);
            CHECK(result.g_w >= 8 && result.g_h >= 16);
            CHECK(result.g_w > insets.left+insets.right && result.g_h > insets.top+insets.bottom);
            if (kinds[k] & (NAME|CLOSER|FULLER|MOVER|LFARROW|RTARROW|HSLIDE)) CHECK(result.g_w >= 76);
            if (kinds[k] & (UPARROW|DNARROW|VSLIDE)) CHECK(result.g_h >= 114);
        }
    }
    CHECK(wm_size_position(&start,NAME|SIZER,2147483647L,2147483647L,8,16,19,19,640,400,&result));
    CHECK((LONG)result.g_x+result.g_w+2 <= 32767 && (LONG)result.g_y+result.g_h+2 <= 32767);
    result.g_x = 99;
    CHECK(!wm_size_position(&start,NAME,40,50,8,16,19,19,640,400,&result) && result.g_x == 99);
    start.g_x = 32730;
    CHECK(!wm_size_position(&start,NAME|SIZER,32767,80,8,16,19,19,640,400,&result) && result.g_x == 99);
    return 0;
}
