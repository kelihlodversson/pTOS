/* Ordinary GEM source, usable with the unchanged GEMlib headers. */
#include <gem.h>
#include "gem_forms.h"
#define VERIFY(c) do { if (!(c)) return 0x7600 | __LINE__; } while (0)
static void setup(OBJECT tree[3], TEDINFO *ted, char *text)
{
    int i;
    for (i = 0; i < 3; i++) {
        tree[i].ob_next = tree[i].ob_head = tree[i].ob_tail = -1;
        tree[i].ob_type = G_IBOX; tree[i].ob_flags = tree[i].ob_state = 0;
        tree[i].ob_x = tree[i].ob_y = 0; tree[i].ob_width = 120; tree[i].ob_height = 80;
    }
    tree[1].ob_type = G_FTEXT; tree[1].ob_flags = OF_EDITABLE;
    tree[1].ob_x = 10; tree[1].ob_y = 30; tree[1].ob_width = 64; tree[1].ob_height = 16;
    tree[1].ob_spec.tedinfo = ted;
    ted->te_ptext = text; ted->te_ptmplt = "______"; ted->te_pvalid = "F";
    ted->te_font = 3; ted->te_just = 0; ted->te_color = 0x1100;
    ted->te_txtlen = 7; ted->te_tmplen = 7;
    tree[2].ob_type = G_BUTTON; tree[2].ob_flags = OF_SELECTABLE|OF_EXIT|OF_DEFAULT|OF_LASTOB;
    tree[2].ob_spec.free_string = "OK";
    tree[2].ob_x = 20; tree[2].ob_y = 60; tree[2].ob_width = 64; tree[2].ob_height = 16;
    objc_add(tree,0,1); objc_add(tree,0,2);
}
int gem_form_checks(short graphics)
{
    OBJECT tree[3] = { { 0 } };
    TEDINFO ted = { 0 };
    char text[7] = "@";
    short index = 77, fresh_index, global[15];
    int i;
    setup(tree,&ted,text);
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    VERIFY(objc_edit(tree,1,0,&index,ED_START) == 1 && index == 77);
    VERIFY(objc_edit(tree,0,0,&index,ED_INIT) == 1 && index == 77);
    VERIFY(form_dial(FMD_START,0,0,0,0,0,0,120,80) == 1);
    VERIFY(form_dial(-1,0,0,1,1,0,0,120,80) == 0);
    VERIFY(objc_edit(tree,1,0,&index,ED_INIT) == graphics);
    if (!graphics) {
        VERIFY(index == 77 && text[0] == '@');
        VERIFY(form_do(tree,0) == 0);
        VERIFY(form_alert(1,"[1][No graphics][OK]") == 0);
        return 0;
    }
    VERIFY(index == 0);
    /* ED_INIT is an output-only index, just like the first form_do edit. */
    VERIFY(objc_edit(tree,1,0,&index,ED_END) == 1);
    VERIFY(mt_objc_edit(tree,1,0,&fresh_index,ED_INIT,global) == 1 && fresh_index == 0);
    index = fresh_index;
    i = objc_edit(tree,1,'a',&index,ED_CHAR);
    VERIFY(i == 1 && index == 1 && text[0] == 'A' && !text[1]);
    VERIFY(mt_objc_edit(tree,1,'2',&index,ED_CHAR,global) == 1 && index == 2 && text[1] == '2');
    VERIFY(objc_edit(tree,1,0x4b00,&index,ED_CHAR) == 1 && index == 1);
    VERIFY(objc_edit(tree,1,'b',&index,ED_CHAR) == 1 && index == 2 && text[1] == 'B' && text[2] == '2');
    VERIFY(objc_edit(tree,1,0x0e08,&index,ED_CHAR) == 1 && index == 1 && text[1] == '2' && !text[2]);
    VERIFY(objc_edit(tree,1,0x537f,&index,ED_CHAR) == 1 && index == 1 && !text[1]);
    VERIFY(objc_edit(tree,1,0x011b,&index,ED_CHAR) == 1 && index == 0 && !text[0]);
    VERIFY(objc_edit(tree,1,0,&index,ED_END) == 1);
    ted.te_pvalid = "9";
    VERIFY(objc_edit(tree,1,0,&index,ED_INIT) == 1 && index == 0);
    VERIFY(objc_edit(tree,1,'x',&index,ED_CHAR) == 1 && index == 0 && !text[0]);
    VERIFY(objc_edit(tree,1,'7',&index,ED_CHAR) == 1 && index == 1 && text[0] == '7');
    VERIFY(objc_edit(tree,1,0,&index,ED_END) == 1);
    index = -1;
    VERIFY(objc_edit(tree,1,'x',&index,ED_CHAR) == 0 && index == -1);
    ted.te_txtlen = 0;
    VERIFY(objc_edit(tree,1,0,&index,ED_INIT) == 0 && index == -1);
    VERIFY(form_alert(1,"[") == 0 && form_alert(4,"[0][Text][OK]") == 0);
    VERIFY(form_error(64) == 0);
    VERIFY(form_dial(FMD_GROW,20,30,4,4,0,0,120,80) == 1);
    VERIFY(form_dial(FMD_SHRINK,20,30,4,4,0,0,120,80) == 1);
    VERIFY(form_dial(FMD_FINISH,0,0,0,0,0,0,120,80) == 1);
    return 0;
}
/* Interactive driver supplies A, 2, Return, then Return for the alert. */
int gem_form_interactive(void)
{
    OBJECT tree[3] = { { 0 } };
    TEDINFO ted = { 0 };
    char text[7] = "@";
    setup(tree,&ted,text);
    VERIFY(objc_draw(tree,0,8,0,0,120,80) == 1);
    gem_form_input_stage(1);
    VERIFY(form_do(tree,0) == 2);
    VERIFY(text[0] == 'A' && text[1] == '2' && !text[2]);
    gem_form_input_stage(2);
    VERIFY(form_alert(1,"[2][Shared AES forms|GEM compatible][OK|Cancel]") == 1);
    return 0;
}
