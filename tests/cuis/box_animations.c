/* Standard GEM animation bindings, shared by host and isolated x32 clients. */
#include <gem.h>
#include "gem_draw_probe.h"
#include "box_animations.h"
#define ANIM_CHECK(c) do { if (!(c)) return 0x8800+__LINE__; } while (0)
int gem_box_animation_checks(short handle)
{
    GRECT small = { 20,30,4,4 }, large = { 0,0,120,80 };
    short global[15], i;
    int before[5];
    static const short points[5][2] = { {20,30}, {0,0}, {58,38}, {100,70}, {40,40} };
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    for (i = 0; i < 5; i++) before[i] = gem_probe_pixel(handle,points[i][0],points[i][1]);
    ANIM_CHECK(graf_mbox(4,4,20,30,60,40) == 1);
    ANIM_CHECK(mt_graf_mbox(4,4,60,40,20,30,global) == 1);
    ANIM_CHECK(graf_mbox(4,4,20,40,60,30) == 1);
    ANIM_CHECK(graf_mbox(4,4,60,30,20,40) == 1);
    ANIM_CHECK(graf_mbox(4,4,20,30,20,30) == 1);
    ANIM_CHECK(graf_growbox(20,30,4,4,0,0,120,80) == 1);
    ANIM_CHECK(mt_graf_growbox(20,30,4,4,0,0,120,80,global) == 1);
    /* GEM's shrink arguments list the small destination first. */
    ANIM_CHECK(graf_shrinkbox(20,30,4,4,0,0,120,80) == 1);
    ANIM_CHECK(mt_graf_shrinkbox(20,30,4,4,0,0,120,80,global) == 1);
    ANIM_CHECK(graf_growbox_grect(&small,&large) == 1 && mt_graf_growbox_grect(&small,&large,global) == 1);
    ANIM_CHECK(graf_shrinkbox_grect(&small,&large) == 1 && mt_graf_shrinkbox_grect(&small,&large,global) == 1);
    ANIM_CHECK(graf_growbox_grect(&small,&small) == 1 && graf_shrinkbox_grect(&small,&small) == 1);
    ANIM_CHECK(small.g_x == 20 && small.g_y == 30 && small.g_w == 4 && small.g_h == 4);
    ANIM_CHECK(large.g_x == 0 && large.g_y == 0 && large.g_w == 120 && large.g_h == 80);
    ANIM_CHECK(wind_update(BEG_UPDATE) == 1);
    ANIM_CHECK(graf_growbox_grect(&small,&large) == 1);
    ANIM_CHECK(wind_update(END_UPDATE) == 1);
    for (i = 0; i < 5; i++) ANIM_CHECK(before[i] == gem_probe_pixel(handle,points[i][0],points[i][1]));
    for (i = 0; i < 15; i++) ANIM_CHECK(global[i] == aes_global[i]);
    ANIM_CHECK(graf_growbox_grect(0,&large) == 0 && mt_graf_shrinkbox_grect(&small,0,global) == 0);
    ANIM_CHECK(graf_mbox(0,4,20,30,60,40) == 0);
    ANIM_CHECK(graf_growbox(20,30,120,80,0,0,4,4) == 0);
    ANIM_CHECK(graf_shrinkbox(20,30,120,80,0,0,4,4) == 0);
    return 0;
}
