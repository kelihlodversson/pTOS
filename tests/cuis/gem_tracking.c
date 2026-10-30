/* Standard GEM tracking, driven through real QEMU pointer packets. */
#include "gem.h"
#include "gem_draw_probe.h"
#include "gem_tracking.h"
#include "ptos.h"
#define TRACK_CHECK(c) do { if (!(c)) return 0x8000+__LINE__; } while (0)
int gem_tracking_checks(void)
{
    OBJECT tree[2] = { { 0 } };
    short stage, mx,my,buttons,mods,rx,ry,handle,w,h,bw,bh,window;
    char ready[] = "x32 GEM graphics tracking 0: READY\r\n";
    static const char pass[] = "x32 GEM graphics tracking: PASS\r\n";
    handle = graf_handle(&w,&h,&bw,&bh); TRACK_CHECK(handle > 0);
    tree[0].ob_next = -1; tree[0].ob_head = tree[0].ob_tail = 1;
    tree[0].ob_type = G_BOX; tree[0].ob_x = tree[0].ob_y = 40;
    tree[0].ob_width = 160; tree[0].ob_height = 80;
    tree[1].ob_next = 0; tree[1].ob_head = tree[1].ob_tail = -1;
    tree[1].ob_type = G_BOX; tree[1].ob_flags = OF_LASTOB;
    tree[1].ob_x = 20; tree[1].ob_y = 10; tree[1].ob_width = 40; tree[1].ob_height = 20;
    /* A held gesture must belong to this application before it can grab it.
     * Focus a plain window so real presses establish content capture. */
    window = wind_create(0,0,0,240,180); TRACK_CHECK(window > 0);
    TRACK_CHECK(wind_open(window,0,0,240,180) == 1 && wind_set(window,WF_TOP,0,0,0,0) == 1);
    for (stage = 0; stage < 4; stage++) {
        int background;
        ready[26] = '0'+stage;
        ptos_write(1,ready,sizeof(ready)-1);
        /* Console output can scroll the framebuffer; sample afterwards. */
        background = gem_probe_pixel(handle,40,40);
        do { TRACK_CHECK(graf_mkstate(&mx,&my,&buttons,&mods) == 1 && evnt_timer(0) == 1); } while (!(buttons & 1));
        if (stage == 0) {
            TRACK_CHECK(graf_rubberbox(40,40,10,10,&rx,&ry) == 1 && rx == 61 && ry == 51);
        } else if (stage == 1) {
            TRACK_CHECK(wind_update(BEG_UPDATE) == 1 && wind_update(BEG_MCTRL) == 1);
            TRACK_CHECK(mt_graf_dragbox(40,30,40,40,20,20,180,120,&rx,&ry,aes_global) == 1 && rx == 160 && ry == 110);
            TRACK_CHECK(wind_update(END_MCTRL) == 1 && wind_update(END_UPDATE) == 1);
        } else if (stage == 2) TRACK_CHECK(graf_slidebox(tree,0,1,0) == 833);
        else {
            tree[1].ob_flags |= OF_FL3DACT;
            TRACK_CHECK(mt_graf_slidebox(tree,0,1,1,aes_global) == 893);
        }
        TRACK_CHECK(tree[1].ob_x == 20 && tree[1].ob_y == 10);
        TRACK_CHECK(gem_probe_pixel(handle,40,40) == background);
    }
    TRACK_CHECK(graf_dragbox(40,30,40,40,20,20,180,120,&rx,&ry) == 1 && rx == 40 && ry == 40);
    TRACK_CHECK(wind_close(window) == 1 && wind_delete(window) == 1);
    ptos_write(1,pass,sizeof(pass)-1);
    return 0;
}
