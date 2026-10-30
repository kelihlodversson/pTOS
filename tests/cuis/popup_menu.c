/* Standard popup-menu source, shared by host and real GEMlib-header clients. */
#include <gem.h>
#include "gem_draw_probe.h"
#include "popup_menu.h"
#define VERIFY(c) do { if (!(c)) return 0x7d00 | __LINE__; } while (0)
int gem_popup_checks(short handle)
{
    OBJECT tree[3] = { { 0 } }, original[3];
    MENU input, output;
    short global[15], i;
    int pixel;
    tree[0].ob_next = -1; tree[0].ob_head = 1; tree[0].ob_tail = 2;
    tree[0].ob_type = G_BOX; tree[0].ob_spec.index = 0xff1170;
    tree[0].ob_x = 10; tree[0].ob_y = 10; tree[0].ob_width = 80; tree[0].ob_height = 40;
    for (i = 1; i <= 2; i++) {
        tree[i].ob_next = i == 2 ? 0 : 2; tree[i].ob_head = tree[i].ob_tail = -1;
        tree[i].ob_type = G_STRING; tree[i].ob_spec.free_string = i == 1 ? "First" : "Second";
        tree[i].ob_y = (i-1)*20; tree[i].ob_width = 80; tree[i].ob_height = 20;
    }
    tree[2].ob_flags = OF_LASTOB;
    for (i = 0; i < 3; i++) original[i] = tree[i];
    input.mn_tree = tree; input.mn_menu = 0; input.mn_item = 1;
    input.mn_scroll = 0; input.mn_keystate = 0;
    output = input;
    gem_popup_input_stage(0);
    pixel = gem_probe_pixel(handle,35,35);
    VERIFY(menu_popup(&input,20,30,&output) == 1);
    VERIFY(output.mn_tree == tree && output.mn_menu == 0 && output.mn_item == 2 && !output.mn_scroll);
    VERIFY(gem_probe_pixel(handle,35,35) == pixel);
    for (i = 0; i < 3; i++) {
        VERIFY(tree[i].ob_state == original[i].ob_state && tree[i].ob_x == original[i].ob_x && tree[i].ob_y == original[i].ob_y);
        VERIFY(tree[i].ob_next == original[i].ob_next && tree[i].ob_head == original[i].ob_head);
    }
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    output.mn_item = 123; output.mn_keystate = 123;
    gem_popup_input_stage(1);
    pixel = gem_probe_pixel(handle,35,35);
    VERIFY(mt_menu_popup(&input,20,30,&output,global) == 0 && !output.mn_keystate && output.mn_item == 123);
    VERIFY(gem_probe_pixel(handle,35,35) == pixel);
    tree[1].ob_state = OS_DISABLED;
    gem_popup_input_stage(2);
    pixel = gem_probe_pixel(handle,35,35);
    VERIFY(menu_popup(&input,20,30,&output) == 0 && !output.mn_keystate && tree[1].ob_state == OS_DISABLED);
    VERIFY(gem_probe_pixel(handle,35,35) == pixel);
    {
        OBJECT nested[5] = { { 0 } };
        MENU alias;
        nested[0].ob_next = -1; nested[0].ob_head = nested[0].ob_tail = 1;
        nested[1] = original[0]; nested[1].ob_next = 0;
        nested[1].ob_head = 3; nested[1].ob_tail = 4;
        nested[3] = original[1]; nested[3].ob_next = 4;
        nested[4] = original[2]; nested[4].ob_next = 1;
        alias = input; alias.mn_tree = nested; alias.mn_menu = 1; alias.mn_item = 3;
        gem_popup_input_stage(3);
        pixel = gem_probe_pixel(handle,35,35);
        VERIFY(menu_popup(&alias,20,30,&alias) == 1);
        VERIFY(alias.mn_tree == nested && alias.mn_menu == 1 && alias.mn_item == 4);
        VERIFY(nested[1].ob_x == original[0].ob_x && !nested[3].ob_state && !nested[4].ob_state);
        VERIFY(gem_probe_pixel(handle,35,35) == pixel);
    }
    input.mn_scroll = 1;
    VERIFY(menu_popup(&input,20,30,&output) == 0);
    input.mn_scroll = 0; input.mn_item = 0;
    VERIFY(menu_popup(&input,20,30,&output) == 0);
    input.mn_item = 1; tree[2].ob_next = 1;
    VERIFY(menu_popup(&input,20,30,&output) == 0);
    tree[2].ob_next = 0; global[2] = -1;
    output.mn_keystate = 123; output.mn_item = 456;
    VERIFY(mt_menu_popup(&input,20,30,&output,global) == 0 && !output.mn_keystate && output.mn_item == 456);
    input.mn_keystate = 123;
    VERIFY(mt_menu_popup(&input,20,30,&input,global) == 0 && !input.mn_keystate && input.mn_item == 1);
    output.mn_keystate = 123;
    VERIFY(menu_popup(0,20,30,&output) == 0 && !output.mn_keystate && output.mn_item == 456);
    VERIFY(menu_popup(&input,20,30,0) == 0);
    return 0;
}
