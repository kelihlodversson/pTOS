/* Compare safe inputs with native rc_constrain; reject WORD endpoint wrap. */
#include "gemmovegeom.h"
#include "rectfunc.h"
#include "aesdefs.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int main(void)
{
    GRECT start = { 20,30,200,140 }, result, expected, bounds = { 0,19,811,10000 };
    LONG x,y;
    for (x = -100; x < 1500; x += 79) for (y = -100; y < 12000; y += 599) {
        expected = start; expected.g_x += x-80; expected.g_y += y-35;
        rc_constrain(&bounds,&expected);
        CHECK(wm_move_position(&start,0,80,35,x,y,640,400,19,19,&result));
        CHECK(result.g_x == expected.g_x && result.g_y == expected.g_y && result.g_w == 200 && result.g_h == 140);
    }
    CHECK(wm_move_position(&start,0,80,35,180,85,640,400,19,19,&result) && result.g_x == 120 && result.g_y == 80);
    CHECK(wm_move_position(&start,0,80,35,2147483647L,2147483647L,640,400,19,19,&result));
    CHECK(result.g_x == 611 && result.g_y == 9879);
    CHECK(wm_move_position(&start,0,80,35,(-2147483647L-1),(-2147483647L-1),640,400,19,19,&result));
    CHECK(result.g_x == 0 && result.g_y == 19);
    start.g_w = start.g_h = 32763;
    CHECK(wm_move_position(&start,0,80,35,32767,32767,32767,400,19,19,&result));
    CHECK((LONG)result.g_x+result.g_w+2 <= 32767 && (LONG)result.g_y+result.g_h+2 <= 32767);
    start.g_x = 0; start.g_y = 30; start.g_w = 32763; start.g_h = 80;
    CHECK(wm_move_position(&start,0,80,35,32767,35,640,400,19,19,&result) && result.g_x == 2);
    start.g_x = 0; start.g_y = 30; start.g_w = 32758; start.g_h = 140;
    CHECK(wm_move_position(&start,MOVER|SIZER,80,35,130,35,640,400,19,19,&result));
    CHECK(result.g_x == 0 && result.g_y == 30 && result.g_w == 32758 && result.g_h == 140);
    start.g_w = 0; result.g_x = 99;
    CHECK(!wm_move_position(&start,0,80,35,90,40,640,400,19,19,&result) && result.g_x == 99);
    return 0;
}
