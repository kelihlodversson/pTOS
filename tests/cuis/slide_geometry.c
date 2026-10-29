/* Compare slider proposals with native rc_constrain/gr_slidebox rounding. */
#include "gemslidegeom.h"
#include "aesdefs.h"
#include "rectfunc.h"
#include "arch/x86_64/asm.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int main(void)
{
    WM_FRAME_STATE state = { 0 };
    WM_SLIDER_GEOMETRY slider;
    GRECT expected, outline;
    WORD sizes[] = { -1,1,250,1000 }, positions[] = { 0,1,500,1000 };
    WORD boxes[] = { 12,19,38 }, widths[] = { 320,320,640 }, heights[] = { 200,400,400 };
    WORD mode, box, size, position, axis, mask, object, value, divnd, divis;
    LONG dx,dy, press_x,press_y;
    state.border.g_x = 20; state.border.g_y = 30; state.border.g_w = 300; state.border.g_h = 240;
    state.hbox = 19; state.adjusted_height = 23; state.name = state.info = "";
    for (mode = 0; mode < 3; mode++) for (box = 0; box < 3; box++) {
        state.wbox = boxes[box];
        state.adjusted_width = state.wbox+4-(widths[mode] < 640 ? 1+(heights[mode] >= 400) : 0);
        for (mask = 0; mask < 16; mask++) for (size = 0; size < 4; size++) for (position = 0; position < 4; position++) {
            state.kind = NAME|SIZER|VSLIDE|HSLIDE|((mask & 1) ? UPARROW : 0)|((mask & 2) ? DNARROW : 0)|
                ((mask & 4) ? LFARROW : 0)|((mask & 8) ? RTARROW : 0);
            state.hslsiz = state.vslsiz = sizes[size]; state.hslide = state.vslide = positions[position];
            for (axis = 0; axis < 2; axis++) {
                object = axis ? W_VELEV : W_HELEV;
                CHECK(wm_slider_geometry(&state,object,&slider));
                press_x = slider.thumb.g_x+3; press_y = slider.thumb.g_y+3;
                for (dx = -400; dx < 500; dx += 137) for (dy = -400; dy < 500; dy += 137) {
                    expected = slider.thumb; expected.g_x += dx; expected.g_y += dy;
                    rc_constrain(&slider.track,&expected);
                    divnd = axis ? expected.g_y-slider.track.g_y : expected.g_x-slider.track.g_x;
                    divis = axis ? slider.track.g_h-slider.thumb.g_h : slider.track.g_w-slider.thumb.g_w;
                    if (divnd) divnd += ADJ3DSTD;
                    if (divnd > divis) divnd = divis;
                    CHECK(wm_slider_position(&slider,press_x,press_y,press_x+dx,press_y+dy,&value,&outline));
                    CHECK(value == (divis ? mul_div_round(divnd,1000,divis) : 0));
                    CHECK(outline.g_x == expected.g_x && outline.g_y == expected.g_y &&
                        outline.g_w == expected.g_w && outline.g_h == expected.g_h);
                }
                CHECK(wm_slider_position(&slider,press_x,press_y,2147483647L,2147483647L,&value,&outline) && value == 1000);
                CHECK(wm_slider_position(&slider,press_x,press_y,(-2147483647L-1),(-2147483647L-1),&value,&outline) && !value);
                CHECK(wm_slider_page(&state,object-1,slider.thumb.g_x,slider.thumb.g_y) == (axis ? WA_UPPAGE : WA_LFPAGE));
                CHECK(wm_slider_page(&state,object-1,slider.thumb.g_x+2,slider.thumb.g_y+2) == (axis ? WA_DNPAGE : WA_RTPAGE));
            }
        }
    }
    state.kind = 0;
    CHECK(!wm_slider_geometry(&state,W_VELEV,&slider) && !wm_slider_geometry(&state,W_SIZER,&slider));
    return 0;
}
