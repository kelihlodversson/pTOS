/* Standard GEM menu bar source, with real mouse interaction in QEMU. */
#include <gem.h>
#include "gem_draw_probe.h"
#include "gem_menubar.h"
#define VERIFY(c) do { if (!(c)) return 0x7e00 | __LINE__; } while (0)
int gem_menubar_checks(short handle)
{
    OBJECT tree[10] = { { 0 } };
    short cw, ch, message[8], result, i, global[15];
    VERIFY(gem_probe_cell(handle,&cw,&ch) == 0);
    for (i = 0; i < 10; i++) {
        tree[i].ob_next = tree[i].ob_head = tree[i].ob_tail = -1;
        tree[i].ob_type = G_IBOX;
    }
    tree[0].ob_head = 1; tree[0].ob_tail = 5;
    tree[1].ob_next = 5; tree[1].ob_head = tree[1].ob_tail = 2;
    tree[1].ob_type = G_BOX; tree[1].ob_spec.index = 0x1170;
    tree[1].ob_width = 120; tree[1].ob_height = ch+3;
    tree[2].ob_next = 1; tree[2].ob_head = 3; tree[2].ob_tail = 4;
    tree[2].ob_width = 120; tree[2].ob_height = ch+3;
    for (i = 3; i <= 4; i++) {
        tree[i].ob_type = G_TITLE; tree[i].ob_next = i == 3 ? 4 : 2;
        tree[i].ob_spec.free_string = i == 3 ? " Desk " : " File ";
        tree[i].ob_x = (i-3)*60; tree[i].ob_width = 60; tree[i].ob_height = ch+3;
    }
    tree[5].ob_next = 0; tree[5].ob_head = 6; tree[5].ob_tail = 7; tree[5].ob_flags = OF_HIDETREE;
    for (i = 6; i <= 7; i++) {
        tree[i].ob_type = G_BOX; tree[i].ob_spec.index = 0xff1170;
        tree[i].ob_x = (i-6)*60; tree[i].ob_y = ch+3;
        tree[i].ob_width = 100; tree[i].ob_height = 40;
        tree[i].ob_next = i == 6 ? 7 : 5; tree[i].ob_head = tree[i].ob_tail = i+2;
        tree[i+2].ob_type = G_STRING; tree[i+2].ob_next = i;
        tree[i+2].ob_spec.free_string = i == 6 ? "About" : "Open";
        tree[i+2].ob_width = 100; tree[i+2].ob_height = 40;
    }
    tree[9].ob_flags = OF_LASTOB;
    gem_menubar_input_stage(0);
    VERIFY(menu_bar(tree,MENU_INSTALL) == 1 && tree[1].ob_width >= 120);
    result = evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,5000,
                        NULL,NULL,NULL,NULL,NULL,NULL);
    VERIFY((result & MU_MESAG) && message[0] == MN_SELECTED && message[1] == 0);
    VERIFY(message[3] == 4 && message[4] == 9 && message[7] == 7);
    VERIFY((((unsigned long)(unsigned short)message[5]<<16) | (unsigned short)message[6]) == ((unsigned long)tree & 0xffffffffUL));
    VERIFY((tree[4].ob_state & OS_SELECTED) && !(tree[3].ob_state & OS_SELECTED));
    VERIFY(menu_tnormal(tree,4,UNHIGHLIGHT) == 1 && !(tree[4].ob_state & OS_SELECTED));
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    global[2] = -1;
    VERIFY(mt_menu_bar(tree,MENU_REMOVE,global) == 0);
    VERIFY(menu_bar(tree,MENU_REMOVE) == 1);
    VERIFY(menu_bar(tree,2) == 0);
    return 0;
}
