/* Ordinary GEM source for menu item state, redraw and text contracts. */
#include <gem.h>
#include "gem_draw_probe.h"
#include "menu_items.h"
#define VERIFY(c) do { if (!(c)) return 0x7c00 | __LINE__; } while (0)
int gem_menu_checks(short handle)
{
    OBJECT title = { 0 }, background = { 0 };
    char text[32] = " Menu ";
    short global[15], i, before[40], changed = 0;
    title.ob_next = title.ob_head = title.ob_tail = -1;
    title.ob_type = G_TITLE; title.ob_spec.free_string = text;
    title.ob_x = title.ob_y = 20; title.ob_width = 60; title.ob_height = 20;
    background = title; background.ob_type = G_BOX; background.ob_spec.index = 0x1170;
    VERIFY(objc_draw(&background,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&title,0,0,0,0,100,100) == 1);
    for (i = 0; i < 40; i++) before[i] = gem_probe_pixel(handle,25+i,22);
    VERIFY(menu_icheck(&title,0,CHECK) == 1 && (title.ob_state & OS_CHECKED));
    VERIFY(menu_ienable(&title,0,DISABLE) == 1 && (title.ob_state & OS_DISABLED));
    VERIFY(menu_tnormal(&title,0,HIGHLIGHT) == 0 && !(title.ob_state & OS_SELECTED));
    for (i = 0; i < 40; i++) VERIFY(before[i] == gem_probe_pixel(handle,25+i,22));
    VERIFY(menu_icheck(&title,0,UNCHECK) == 1 && !(title.ob_state & OS_CHECKED));
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    VERIFY(mt_menu_ienable(&title,0,ENABLE,global) == 1 && !title.ob_state);
    VERIFY(mt_menu_tnormal(&title,0,HIGHLIGHT,global) == 1 && (title.ob_state & OS_SELECTED));
    for (i = 0; i < 40; i++) if (before[i] != gem_probe_pixel(handle,25+i,22)) changed++;
    VERIFY(changed > 0);
    VERIFY(menu_tnormal(&title,0,UNHIGHLIGHT) == 1 && !title.ob_state);
    for (i = 0; i < 40; i++) VERIFY(before[i] == gem_probe_pixel(handle,25+i,22));
    VERIFY(menu_ienable(&title,(short)0x8000,DISABLE) == 1 && (title.ob_state & OS_DISABLED));
    VERIFY(mt_menu_ienable(&title,(short)0x8000,ENABLE,global) == 1 && !title.ob_state);
    VERIFY(menu_text(&title,0,"New") == 1 && title.ob_spec.free_string == text);
    VERIFY(text[0] == 'N' && text[1] == 'e' && text[2] == 'w' && !text[3]);
    VERIFY(mt_menu_text(&title,0,"",global) == 1 && !text[0]);
    VERIFY(mt_menu_icheck(&title,0,CHECK,global) == 1 && (title.ob_state & OS_CHECKED));
    global[2] = -1;
    VERIFY(mt_menu_icheck(&title,0,UNCHECK,global) == 0 && (title.ob_state & OS_CHECKED));
    VERIFY(mt_menu_ienable(&title,0,DISABLE,global) == 0 && !(title.ob_state & OS_DISABLED));
    VERIFY(mt_menu_tnormal(&title,0,HIGHLIGHT,global) == 0 && !(title.ob_state & OS_SELECTED));
    VERIFY(mt_menu_text(&title,0,"Bad",global) == 0 && !text[0]);
    VERIFY(menu_icheck(0,0,CHECK) == 0 && menu_text(&title,-1,"Bad") == 0);
    return 0;
}
