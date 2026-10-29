/* Standard GEM source, real VDI pixel checks, and a userspace object demo. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#include "gem_draw_probe.h"
#include "mono_objects.h"
#include "colour_objects.h"
#include "three_d.h"
#include "menu_items.h"
#include "popup_menu.h"
#include "gem_menubar.h"
#include "gem_layout.h"
#include "form_keys.h"
#include "gem_buttons.h"
#include "gem_forms.h"
#include "gem_resources.h"
#include "control_message.h"
#define VERIFY(c) do { if (!(c)) return 0x7900 | __LINE__; } while (0)
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
void gem_form_input_stage(short stage)
{
    static const char form[] = "x32 GEM form input: READY\r\n";
    static const char alert[] = "x32 GEM alert input: READY\r\n";
    if (stage == 1) ptos_write(1,form,sizeof(form)-1);
    else ptos_write(1,alert,sizeof(alert)-1);
}
void gem_popup_input_stage(short stage)
{
    char message[] = "x32 GEM popup 0: READY\r\n";
    message[14] = '0'+stage;
    ptos_write(1,message,sizeof(message)-1);
}
void gem_menubar_input_stage(short stage)
{
    static const char message[] = "x32 GEM menu bar: READY\r\n";
    (void)stage;
    ptos_write(1,message,sizeof(message)-1);
}
/* Compiles against unmodified GEMlib as well as the kernel's GEM header. */
static int control_checks(void)
{
    short id, stage, result, message[8], x,y,w,h, buttons;
    short queued[8] = { 0x4712,0,0,0,0,0,0,0 };
    char ready[] = "x32 GEM frame control 0: READY\r\n";
    static const char pass[] = "x32 GEM frame controls: PASS\r\n";
    id = wind_create(NAME|CLOSER|FULLER|MOVER|SIZER,0,0,200,140);
    VERIFY(id > 0 && wind_set_str(id,WF_NAME,"Close and full") == 1);
    VERIFY(wind_open(id,20,30,200,140) == 1);
    while (appl_read(-1,16,message) == 1) { }
    for (stage = 0; stage < 7; stage++) {
        ready[22] = '0'+stage;
        ptos_write(1,ready,sizeof(ready)-1);
        if (stage == 3 || stage == 5) {
            static const char poll_pass[] = "x32 GEM mover poll: PASS\r\n";
            static const char size_poll[] = "x32 GEM sizer poll: PASS\r\n";
            do {
                VERIFY(evnt_timer(0) == 1 && graf_mkstate(NULL,NULL,&buttons,NULL) == 1);
            } while (!(buttons & 1));
            VERIFY(appl_write(aes_global[2],16,queued) == 1);
            VERIFY(appl_read(-1,16,message) == 1 && message[0] == 0x4712);
            VERIFY(appl_read(-1,16,message) == 0);
            if (stage == 5) ptos_write(1,size_poll,sizeof(size_poll)-1);
            else ptos_write(1,poll_pass,sizeof(poll_pass)-1);
        }
        for (;;) {
            if (stage == 4 || stage == 6) {
                result = appl_read(-1,16,message) == 1 ? MU_MESAG : 0;
                if (!result) { VERIFY(evnt_timer(0) == 1); continue; }
            } else result = evnt_multi(MU_MESAG|MU_BUTTON|MU_KEYBD|MU_TIMER,1,1,1,
                0,0,0,0,0,0,0,0,0,0,message,10000,NULL,NULL,NULL,NULL,NULL,NULL);
            VERIFY(result && !(result & (MU_BUTTON|MU_TIMER)));
            if (result & MU_MESAG) {
                if (message[0] == WM_REDRAW) continue;
                VERIFY(stage && message[0] == (stage == 1 ? WM_FULLED : stage == 2 ? WM_CLOSED : stage >= 5 ? WM_SIZED : WM_MOVED));
                VERIFY(message[3] == id && message[4] == (stage == 3 || stage == 4 ? 80 : 20) && message[5] == (stage == 3 || stage == 4 ? 70 : 30) &&
                    message[6] == (stage >= 5 ? 241 : 200) && message[7] == (stage >= 5 ? 171 : 140));
                break;
            }
            if (result & MU_KEYBD) { VERIFY(!stage); break; }
        }
        VERIFY(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && x == 20 && y == 30 && w == 200 && h == 140);
        VERIFY(evnt_multi(MU_BUTTON|MU_TIMER,1,1,1,0,0,0,0,0,0,0,0,0,0,NULL,0,
            NULL,NULL,NULL,NULL,NULL,NULL) == MU_TIMER);
    }
    VERIFY(wind_close(id) == 1 && wind_delete(id) == 1);
    ptos_write(1,pass,sizeof(pass)-1);
    return 0;
}

static void object(OBJECT *o, short type, short x, short y, short w, short h)
{
    o->ob_next = o->ob_head = o->ob_tail = -1;
    o->ob_type = type; o->ob_flags = o->ob_state = 0;
    o->ob_x = x; o->ob_y = y; o->ob_width = w; o->ob_height = h;
}
static char mutation_text[200];
static int mutation_kind;
static short mutate_callback(PARMBLK *pb)
{
    OBJECT *target = (OBJECT *)(uintptr_t)pb->pb_parm;
    if (!mutation_kind) target->ob_spec.free_string = mutation_text;
    else if (mutation_kind == 1) target->ob_spec.tedinfo->te_ptext = mutation_text;
    else target->ob_spec.tedinfo->te_ptmplt = mutation_text;
    return 0;
}
static int mutation_checks(void)
{
    OBJECT tree[3] = { { 0 } };
    TEDINFO ted = { 0 };
    USERBLK user;
    int i;
    for (i = 0; i < 199; i++) mutation_text[i] = 'X';
    object(&tree[0],G_IBOX,0,0,100,100);
    object(&tree[1],G_USERDEF,0,0,20,20); tree[1].ob_spec.userblk = &user;
    object(&tree[2],G_STRING,30,0,60,20);
    user.ub_code = mutate_callback; user.ub_parm = (intptr_t)&tree[2];
    ted.te_font = 3; ted.te_color = 0x1100;
    VERIFY(objc_add(tree,0,1) == 1 && objc_add(tree,0,2) == 1);
    for (mutation_kind = 0; mutation_kind < 3; mutation_kind++) {
        ted.te_ptext = "ok"; ted.te_ptmplt = "__";
        tree[2].ob_type = mutation_kind == 0 ? G_STRING : mutation_kind == 1 ? G_TEXT : G_FTEXT;
        tree[2].ob_spec.tedinfo = &ted;
        if (!mutation_kind) tree[2].ob_spec.free_string = "ok";
        VERIFY(objc_draw(tree,0,1,0,0,100,100) == 0);
    }
    return 0;
}
static int geometry_checks(WORD handle)
{
    OBJECT hidden[5] = { { 0 } }, large = { 0 };
    WORD reference[16][17];
    int black, x, y, axis, style;
    object(&hidden[0],G_BOX,0,0,100,100); hidden[0].ob_spec.index = 0x1170;
    object(&hidden[1],G_TEXT,0,0,20,20); hidden[1].ob_flags = OF_HIDETREE;
    object(&hidden[2],G_IBOX,0,0,20,20); hidden[2].ob_flags = OF_HIDETREE|OF_INDIRECT;
    VERIFY(objc_add(hidden,0,1) == 1 && objc_add(hidden,0,2) == 1);
    VERIFY(objc_draw(hidden,0,1,0,0,100,100) == 1);
    object(&large,G_BOX,10,10,10000,20); large.ob_spec.index = 0x1172;
    VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,99,10) == 2 && gem_probe_pixel(handle,99,29) == 2);
    VERIFY(gem_probe_pixel(handle,99,30) == 0);
    VERIFY(objc_draw(hidden,0,1,0,0,100,100) == 1);
    large.ob_width = 32767;
    VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1 && gem_probe_pixel(handle,99,20) == 2);
    large.ob_width = 20; large.ob_height = 32767;
    VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1 && gem_probe_pixel(handle,20,99) == 2);
    large.ob_width = 10000; large.ob_height = 20;
    VERIFY(objc_draw(hidden,0,1,0,0,100,100) == 1);
    large.ob_type = G_IBOX; large.ob_spec.index = 0x11100;
    /* Use the native line renderer's black pixel as the reference; the
     * truecolor VDI readback need not report its index as logical pen 1. */
    large.ob_width = 20;
    VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1);
    black = gem_probe_pixel(handle,10,10); VERIFY(black != 0);
    VERIFY(objc_draw(hidden,0,1,0,0,100,100) == 1);
    large.ob_width = 10000;
    VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,99,10) == black && gem_probe_pixel(handle,99,29) == black);
    VERIFY(gem_probe_pixel(handle,10,20) == black && gem_probe_pixel(handle,99,20) == 0);
    VERIFY(objc_draw(hidden,0,1,0,0,100,100) == 1);
    large.ob_x = -10010; large.ob_width = 10030;
    VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,0,10) == black && gem_probe_pixel(handle,19,20) == black);
    VERIFY(gem_probe_pixel(handle,0,20) == 0);
    large.ob_x = large.ob_y = 10; large.ob_type = G_BOX;
    for (axis = 0; axis < 2; axis++) for (style = 0; style < 5; style++) {
        large.ob_width = axis ? 20 : 32767; large.ob_height = axis ? 32767 : 20;
        large.ob_state = style == 2 ? OS_OUTLINED : style == 3 ? OS_CROSSED : style == 4 ? OS_SHADOWED : 0;
        large.ob_spec.index = style == 0 ? 0x11172 : (style == 1 || style == 4) ? 0xff1172 : 0x1172;
        VERIFY(objc_draw(hidden,0,0,0,0,100,100) == 1);
        VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1 && gem_probe_pixel(handle,15,15) == 2);
        if (style == 3) {
            VERIFY(gem_probe_pixel(handle,axis ? 10 : 50,axis ? 50 : 10) == 0);
            VERIFY(gem_probe_pixel(handle,axis ? 29 : 50,axis ? 50 : 29) == 0);
        } else if (style == 4) {
            VERIFY(gem_probe_pixel(handle,axis ? 31 : 50,axis ? 50 : 31) == black);
            VERIFY(gem_probe_pixel(handle,axis ? 33 : 50,axis ? 50 : 33) == 0);
        } else if (style == 2) {
            VERIFY(gem_probe_pixel(handle,axis ? 7 : 50,axis ? 50 : 7) == black);
            VERIFY(gem_probe_pixel(handle,axis ? 8 : 50,axis ? 50 : 8) == 0);
        } else {
            int edge = style == 0 ? 10 : 9;
            VERIFY(gem_probe_pixel(handle,axis ? edge : 50,axis ? 50 : edge) == black);
        }
        VERIFY(gem_probe_pixel(handle,axis ? 15 : 99,axis ? 99 : 15) == 2);
    }
    large.ob_state = 0;
    object(&hidden[3],G_BUTTON,10,10,25000,20); hidden[3].ob_spec.free_string = "Wide";
    object(&hidden[4],G_BOX,40,60,8,8); hidden[4].ob_spec.index = 0x1174;
    VERIFY(objc_add(hidden,0,3) == 1 && objc_add(hidden,0,4) == 1);
    VERIFY(objc_draw(hidden,0,1,0,0,100,100) == 1 && gem_probe_pixel(handle,40,60) == 4);
    VERIFY(objc_draw(hidden,0,0,0,0,100,100) == 1);
    object(&large,G_STRING,10,40,80,16); large.ob_spec.free_string = "ABCDE";
    VERIFY(objc_draw(&large,0,0,0,0,100,100) == 1);
    for (y = 0; y < 16; y++) for (x = 0; x < 17; x++)
        reference[y][x] = gem_probe_pixel(handle,x+20,y+40);
    VERIFY(objc_draw(hidden,0,0,0,0,100,100) == 1);
    large.ob_x = -10;
    VERIFY(objc_draw(&large,0,0,0,40,17,16) == 1);
    for (y = 0; y < 16; y++) for (x = 0; x < 17; x++)
        VERIFY(gem_probe_pixel(handle,x,y+40) == reference[y][x]);
    return 0;
}
static short callback(PARMBLK *pb)
{
    OBJECT box = { 0 };
    if (pb->pb_obj || pb->pb_x != 20 || pb->pb_y != 20 || pb->pb_parm != 123) return -1;
    object(&box,G_BOX,pb->pb_x+2,pb->pb_y+2,4,4);
    box.ob_spec.index = 0x1173;
    if (!objc_draw(&box,0,0,pb->pb_xc,pb->pb_yc,pb->pb_wc,pb->pb_hc)) return -1;
    return 0;
}
static short fault_callback(PARMBLK *pb)
{
    (void)pb;
    *(volatile ULONG *)(uintptr_t)fault_callback = 0;
    return 0;
}
static int demo(WORD width, WORD height)
{
    OBJECT scene[16] = { { 0 } };
    static TEDINFO title, field;
    short i, x = (width-580)/2, y = (height-380)/2;
    short frame, wx, wy, ww, wh;
    OBJECT work = { 0 };
    object(&scene[0],G_BOX,0,0,width,height); scene[0].ob_spec.index = 0x1143;
    object(&scene[1],G_BOX,x,y,580,380); scene[1].ob_spec.index = 0x11178;
    scene[1].ob_state = OS_OUTLINED|OS_SHADOWED;
    object(&scene[2],G_BOX,1,1,578,28); scene[2].ob_spec.index = 0x1174;
    object(&scene[3],G_TEXT,14,0,500,28);
    title.te_ptext = "pTOS / libaes - object rendering"; title.te_font = 3; title.te_color = 0x1000;
    scene[3].ob_spec.tedinfo = &title;
    object(&scene[4],G_BOXCHAR,551,4,20,20); scene[4].ob_spec.index = 0x58011178;
    object(&scene[5],G_STRING,24,50,530,24); scene[5].ob_spec.free_string = "Object drawing in user mode";
    object(&scene[6],G_STRING,24,78,530,20); scene[6].ob_spec.free_string = "Shared libaes.so  |  Existing VDI services";
    object(&scene[7],G_BOX,24,116,532,130); scene[7].ob_spec.index = 0x11170;
    object(&scene[8],G_STRING,12,8,490,20); scene[8].ob_spec.free_string = "Classic GEM object states";
    object(&scene[9],G_BUTTON,14,44,128,30); scene[9].ob_spec.free_string = "Normal";
    object(&scene[10],G_BUTTON,166,44,128,30); scene[10].ob_spec.free_string = "Selected"; scene[10].ob_state = OS_SELECTED;
    object(&scene[11],G_BUTTON,318,44,164,30); scene[11].ob_spec.free_string = "Disabled"; scene[11].ob_state = OS_DISABLED;
    object(&scene[12],G_STRING,14,88,300,22); scene[12].ob_spec.free_string = "  Checked entry"; scene[12].ob_state = OS_CHECKED;
    object(&scene[13],G_FBOXTEXT,24,268,300,28);
    field.te_ptext = "PTOS"; field.te_ptmplt = "Name: ________"; field.te_font = 3;
    field.te_color = 0x1170; field.te_thickness = 1;
    scene[13].ob_spec.tedinfo = &field;
    object(&scene[14],G_BUTTON,408,268,144,32); scene[14].ob_spec.free_string = "Continue";
    scene[14].ob_flags = OF_SELECTABLE|OF_EXIT|OF_DEFAULT;
    object(&scene[15],G_STRING,24,324,530,22); scene[15].ob_spec.free_string = "Classic GEM. Familiar applications. Portable pTOS.";
    VERIFY(objc_add(scene,0,1) == 1 && objc_add(scene,1,2) == 1);
    VERIFY(objc_add(scene,2,3) == 1 && objc_add(scene,2,4) == 1);
    for (i = 5; i <= 7; i++) VERIFY(objc_add(scene,1,i) == 1);
    for (i = 8; i <= 12; i++) VERIFY(objc_add(scene,7,i) == 1);
    for (i = 13; i <= 15; i++) VERIFY(objc_add(scene,1,i) == 1);
    VERIFY(objc_draw(scene,0,8,0,0,width,height) == 1);
    frame = wind_create(NAME|INFO,0,0,width,height);
    VERIFY(frame > 0 && wind_set_str(frame,WF_NAME,"Native GEM window") == 1);
    VERIFY(wind_set_str(frame,WF_INFO,"Shared AES frame builder") == 1);
    VERIFY(wind_open(frame,width-300,height-190,260,140) == 1);
    VERIFY(wind_get(frame,WF_WORKXYWH,&wx,&wy,&ww,&wh) == 1);
    object(&work,G_BOX,wx,wy,ww,wh); work.ob_spec.index = 0x1170;
    VERIFY(objc_draw(&work,0,0,wx,wy,ww,wh) == 1);

    {
        static const char ready[] = "x32 GEM object screenshot: READY\r\n";
        ptos_write(1,ready,sizeof(ready)-1);
    }
    /* Keep the serial test marker out of the visual framebuffer. */
    VERIFY(objc_draw(scene,0,0,0,height-32,width,32) == 1);
    VERIFY(evnt_timer(1800) == 1);
    VERIFY(wind_close(frame) == 1 && wind_delete(frame) == 1);
    return 0;
}
int main(int argc, char **argv, char **envp)
{
    OBJECT box = { 0 };
    USERBLK user;
    WORD handle, width, height, cellw, cellh, pixels[400];
    short x,y,global[15];
    LONG child, status, update;
    short control[8] = { 0 };
    char tail[3] = { 1,'f',0 };
    static const char environment[] = { 0,0 };
    (void)argv; (void)envp;
    VERIFY(appl_init() > 0 && mt_appl_init(global) > 0);
    object(&box,G_BOX,0,0,100,100); box.ob_spec.index = 0x1170;
    if (argc > 1 && argv[1][0] == 'l') {
        short parent = (short)process(0x10c,0,0,0,0);
        VERIFY(parent > 0 && wind_update(BEG_UPDATE) == 1);
        control[0] = 0x7001; control[1] = global[2];
        VERIFY(appl_write(parent,16,control) == 1);
        /* Deliberately wait for the parent's mouse while owning display. */
        VERIFY(wind_update(BEG_MCTRL) == 1);
        control[0] = 0x7002;
        VERIFY(appl_write(parent,16,control) == 1);
        VERIFY(wind_update(END_MCTRL) == 1 && wind_update(END_UPDATE) == 1 && appl_exit() == 1);
        return 0;
    }

    if (argc > 1 && (argv[1][0] == 'f' || argv[1][0] == 'b')) {
        box.ob_type = G_USERDEF; box.ob_spec.userblk = &user;
        user.ub_code = fault_callback; user.ub_parm = 0;
        if (argv[1][0] == 'b') (void)graf_watchbox(&box,0,OS_SELECTED,0);
        else (void)objc_draw(&box,0,0,0,0,100,100);
        return 99;
    }
    if (gem_probe_open(&handle,&width,&height) != 0) {
        GRECT untouched = { 1,2,3,4 };
        status = gem_resource_native(0,0,0); if (status) return status;
        status = gem_resource_checks(0,0,0); if (status) return status;
        status = gem_form_checks(0); if (status) return status;
        status = gem_button_checks(0); if (status) return status;
        status = gem_form_key_checks(-1); if (status) return status;
        box.ob_x = 7; box.ob_y = 9;
        VERIFY(rsrc_obfix(&box,0) == 0 && box.ob_x == 7 && box.ob_y == 9);
        VERIFY(form_center_grect(&box,&untouched) == 0 && box.ob_x == 7 && untouched.g_x == 1);
        VERIFY(menu_bar(&box,MENU_INSTALL) == 0 && menu_bar(NULL,MENU_REMOVE) == 1);
        VERIFY(objc_draw(&box,0,0,0,0,100,100) == 0 && appl_exit() == 1);
        {
            static const char marker[] = "x32 GEM object drawing: unavailable\r\n";
            ptos_write(1,marker,sizeof(marker)-1);
        }
        return 0;
    }
    if (argc > 1 && argv[1][0] == 'p') {
        char *text;
        /* ARGV[0] deliberately names another directory. AT_EXECFN must win,
         * and a post-start chdir must not move the remembered app directory. */
        VERIFY(process(0x3b,(uintptr_t)"\\GEMSYS",0,0,0) == 0);
        VERIFY(rsrc_load("GEMTEST.RSC") == 1 && rsrc_gaddr(R_STRING,0,&text) == 1);
        VERIFY(text[0] == 'A' && text[4] == 'f');
        /* This name exists only on PATH, not next to the executable. */
        VERIFY(rsrc_load("GEMPATH.RSC") == 1 && rsrc_gaddr(R_STRING,0,&text) == 1);
        VERIFY(text[0] == 'A' && rsrc_free() == 1);
        VERIFY(gem_probe_close(handle) == 0 && appl_exit() == 1);
        return 0;
    }
    if (argc > 1 && (argv[1][0] == 'r' || argv[1][0] == 'R')) {
        OBJECT *tree;
        short parent = (short)process(0x10c,0,0,0,0);
        VERIFY(rsrc_load("GEMTEST.RSC") == 1 && rsrc_gaddr(R_TREE,0,&tree) == 1);
        if (argv[1][0] == 'R') { fault_callback(NULL); return 99; }
        tree[0].ob_x = 1234;
        control[0] = 0x7010; control[1] = global[2];
        VERIFY(appl_write(parent,16,control) == 1 && control_message(control) == 1 && control[0] == 0x7011);
        VERIFY(tree[0].ob_x == 1234 && rsrc_gaddr(R_TREE,0,&tree) == 1);
        /* appl_exit reclaims even resources not explicitly freed. */
        VERIFY(gem_probe_close(handle) == 0 && appl_exit() == 1);
        VERIFY(aes_global[5] == 0 && aes_global[6] == 0 && aes_global[7] == 0 && aes_global[8] == 0);
        return 0;
    }
    if (argc > 1 && argv[1][0] == 'c') {
        box.ob_spec.index = 0x1172;
        VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1);
        VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 1);
        VERIFY(objc_change(&box,0,0,0,0,100,100,0,1) == 1);
        VERIFY(gem_probe_close(handle) == 0 && appl_exit() == 1);
        return 0;
    }
    if (argc > 1 && argv[1][0] == 'm') {
        short parent = (short)process(0x10c,0,0,0,0), window;
        VERIFY(gem_menubar_install(handle,3) == 1);
        window = wind_create(0,300,100,100,80);
        VERIFY(window > 0 && wind_open(window,300,100,100,80) == 1);
        control[0] = 0x7020; control[1] = global[2];
        VERIFY(appl_write(parent,16,control) == 1);
        VERIFY(control_message(control) == 1 && control[0] == 0x7021);
        VERIFY(wind_set(window,WF_TOP,0,0,0,0) == 1 && evnt_timer(0) == 1);
        control[0] = 0x7022;
        VERIFY(appl_write(parent,16,control) == 1);
        VERIFY(control_message(control) == 1 && control[0] == 0x7023);
        VERIFY(appl_exit() == 1); /* Exit while owning the surface. */
        return 0;
    }
    status = process(0x3d,(uintptr_t)"FORMTEST.ON",0,0,0);
    if (status >= 0) {
        short overlay_window;
        static const char pass[] = "x32 GEM modal forms: PASS\r\n";
        VERIFY(process(0x3e,status,0,0,0) == 0);
        status = control_checks(); if (status) return status;
        /* Leave a decorated redraw pending beneath the alert and subsequent
         * modal scopes. Their recursive waits must not repaint its frame. */
        overlay_window = wind_create(NAME|INFO,0,0,240,140);
        VERIFY(overlay_window > 0 && wind_set_str(overlay_window,WF_NAME,"Behind overlays") == 1);
        VERIFY(wind_open(overlay_window,width/2-100,height/2-30,240,140) == 1);
        status = gem_form_interactive(); if (status) return status;
        ptos_write(1,pass,sizeof(pass)-1);
        status = gem_popup_checks(handle); if (status) return status;
        {
            static const char popup_pass[] = "x32 GEM popup menus: PASS\r\n";
            ptos_write(1,popup_pass,sizeof(popup_pass)-1);
            {
                short queued = wind_create(0,0,19,200,120);
                short message[8], mx, my, buttons, keys, key, clicks;
                short state;
                VERIFY(queued > 0 && wind_open(queued,0,19,200,120) == 1);
                VERIFY(wind_set(queued,WF_TOP,0,0,0,0) == 1);
                while (appl_read(-1,16,message) == 1) { }
                status = gem_menubar_checks(handle); if (status) return status;
                /* Menu tracking must preserve the earlier application click. */
                for (state = 1; state >= 0; state--) {
                    VERIFY(evnt_multi(MU_BUTTON|MU_TIMER,1,1,state,
                        0,0,0,0,0,0,0,0,0,0,message,0,
                        &mx,&my,&buttons,&keys,&key,&clicks) & MU_BUTTON);
                    VERIFY(mx == 80 && my == 33 && buttons == state);
                }
                VERIFY(wind_close(queued) == 1 && wind_delete(queued) == 1);
            }
            {
                static const char menu_pass[] = "x32 GEM menu bar: PASS\r\n";
                VERIFY(wind_close(overlay_window) == 1 && wind_delete(overlay_window) == 1);
                ptos_write(1,menu_pass,sizeof(menu_pass)-1);
                {
                    short window = wind_create(0,20,100,100,80);
                    VERIFY(window > 0 && wind_open(window,20,100,100,80) == 1);
                    VERIFY(gem_menubar_install(handle,2) == 1);
                    tail[1] = 'm';
                    child = process(0x4b,100,(uintptr_t)"GEMDRAW.TOS",(uintptr_t)tail,(uintptr_t)environment);
                    VERIFY(child > 0 && control_message(control) == 1 && control[0] == 0x7020);
                    VERIFY(gem_probe_pixel(handle,130,5) == 3);
                    VERIFY(wind_set(window,WF_TOP,0,0,0,0) == 1 && evnt_timer(0) == 1);
                    VERIFY(gem_probe_pixel(handle,130,5) == 2);
                    control[0] = 0x7021;
                    VERIFY(appl_write((short)child,16,control) == 1);
                    VERIFY(control_message(control) == 1 && control[0] == 0x7022);
                    VERIFY(gem_probe_pixel(handle,130,5) == 3);
                    control[0] = 0x7023;
                    VERIFY(appl_write((short)child,16,control) == 1);
                    VERIFY(process(0x13a,child,0,0,0) == (LONG)((ULONG)child<<16));
                    VERIFY(evnt_timer(0) == 1 && gem_probe_pixel(handle,130,5) == 2);
                    VERIFY(menu_bar(NULL,MENU_REMOVE) == 1);
                    VERIFY(wind_close(window) == 1 && wind_delete(window) == 1);
                    {
                        static const char handoff[] = "x32 GEM menu handoff: PASS\r\n";
                        ptos_write(1,handoff,sizeof(handoff)-1);
                    }
                }
            }
        }
    }
    VERIFY(gem_probe_cell(handle,&cellw,&cellh) == 0);
    {
        static const char resource_env[] = "PATH=C:\\NONE;C:\\RPATH\0ARGV=\0WRONG\\NAME.TOS\0p\0\0";
        char argv_tail[2] = { 127,0 };
        VERIFY(process(0x4b,0,(uintptr_t)"APPS\\GEMPATH.TOS", (uintptr_t)argv_tail,(uintptr_t)resource_env) == 0);
    }
    tail[1] = 'r';
    child = process(0x4b,100,(uintptr_t)"GEMDRAW.TOS",(uintptr_t)tail,(uintptr_t)environment);
    VERIFY(child > 0 && control_message(control) == 1 && control[0] == 0x7010);
    status = gem_resource_checks(1,cellw,cellh); if (status) return status;
    status = gem_resource_native(1,cellw,cellh); if (status) return status;
    status = gem_resource_original(); if (status) return status;
    control[0] = 0x7011;
    VERIFY(appl_write((short)child,16,control) == 1);
    VERIFY(process(0x13a,child,0,0,0) == (LONG)((ULONG)child<<16));
    tail[1] = 'R';
    child = process(0x4b,100,(uintptr_t)"GEMDRAW.TOS",(uintptr_t)tail,(uintptr_t)environment);
    VERIFY(child > 0 && process(0x13a,child,0,0,0) == (LONG)(((ULONG)child<<16)|0xb00));
    tail[1] = 'f';
    {
        static const char marker[] = "x32 GEM resource loading: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    status = gem_form_checks(1); if (status) return status;
    status = gem_button_checks(1); if (status) return status;
    status = gem_form_key_checks(handle); if (status) return status;
    status = gem_layout_checks(handle,width,height); if (status) return status;
    status = mutation_checks(); if (status) return status;
    status = gem_colour_checks(handle); if (status) return status;
    status = gem_colour_resource_checks(handle); if (status) return status;
    {
        static const char marker[] = "x32 GEM colour icons: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    status = gem_menu_checks(handle); if (status) return status;
    {
        static const char marker[] = "x32 GEM menu items: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    status = gem_3d_checks(handle); if (status) return status;
    {
        static const char marker[] = "x32 GEM 3D objects: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    status = gem_mono_checks(handle); if (status) return status;
    status = geometry_checks(handle); if (status) return status;
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1);
    update = gem_probe_update_begin(); VERIFY(update >= 0);
    status = gem_button_checks(1); if (status) return status;
    box.ob_spec.index = 0x1172;
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 2 && gem_probe_pixel(handle,4,4) == 0);
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) != 2 && gem_probe_pixel(handle,4,4) == 0);
    VERIFY(objc_change(&box,0,0,0,0,100,100,0,1) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 2 && gem_probe_pixel(handle,4,4) == 0);
    VERIFY(gem_probe_update_end(update) == 0);
    box.ob_spec.index = 0x1170;
    VERIFY(wind_update(BEG_UPDATE) == 1);
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1 && gem_probe_pixel(handle,26,26) == 0);
    object(&box,G_BOX,20,20,20,20); box.ob_spec.index = 0x1172;
    VERIFY(mt_objc_draw(&box,0,0,25,25,10,10,global) == 1);
    VERIFY(gem_probe_pixel(handle,26,26) == 2 && gem_probe_pixel(handle,24,24) == 0 && gem_probe_pixel(handle,35,35) == 0);
    /* Native high-colour selection redraws coloured boxes.  Capture a fully
     * painted object before checking selection/deselection restoration. */
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1);
    for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) pixels[y*20+x] = gem_probe_pixel(handle,x+20,y+20);
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 1 && gem_probe_pixel(handle,26,26) != 2);
    VERIFY(mt_objc_change(&box,0,0,0,0,100,100,0,1,global) == 1);
    for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) VERIFY(gem_probe_pixel(handle,x+20,y+20) == pixels[y*20+x]);
    box.ob_type = G_IBOX;
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_CHECKED,1) == 1);
    VERIFY(gem_probe_pixel(handle,35,35) == pixels[315]);
    box.ob_state = 0;
    box.ob_type = G_USERDEF; box.ob_spec.userblk = &user;
    user.ub_code = callback; user.ub_parm = 123;
    VERIFY(objc_draw(&box,0,0,20,20,10,10) == 1 && gem_probe_pixel(handle,23,23) == 3);
    VERIFY(wind_update(END_UPDATE) == 1);
    child = process(0x4b,100,(uintptr_t)"C:\\GEMDRAW.TOS",(uintptr_t)tail,(uintptr_t)environment);
    VERIFY(child > 0);
    status = process(0x13a,child,0,0,0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 0x0b00));
    tail[1] = 'b';
    child = process(0x4b,100,(uintptr_t)"C:\\GEMDRAW.TOS",(uintptr_t)tail,(uintptr_t)environment);
    VERIFY(child > 0);
    status = process(0x13a,child,0,0,0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 0x0b00));
    VERIFY(wind_update(BEG_MCTRL|NO_BLOCK) == 1 && wind_update(END_MCTRL) == 1);
    /* Reproduce the cross-client cycle: parent owns mouse, child owns
     * display and blocks for mouse, then parent attempts tracking redraws. */
    VERIFY(wind_update(BEG_MCTRL) == 1);
    tail[1] = 'l';
    child = process(0x4b,100,(uintptr_t)"C:\\GEMDRAW.TOS",(uintptr_t)tail,(uintptr_t)environment);
    VERIFY(child > 0 && control_message(control) == 1 && control[0] == 0x7001);
    VERIFY(wind_update(BEG_UPDATE|NO_BLOCK) == 0);
    object(&box,G_BOX,10,10,20,20); box.ob_spec.index = 0x1172;
    VERIFY(graf_watchbox(&box,0,OS_SELECTED,0) == 0 && !box.ob_state);
    {
        OBJECT pair[2] = { { 0 } };
        short next = 99;
        object(&pair[0],G_IBOX,0,0,100,100); pair[1] = box;
        pair[1].ob_flags = OF_SELECTABLE|OF_EXIT;
        VERIFY(objc_add(pair,0,1) == 1);
        VERIFY(form_button(pair,1,1,&next) == 0 && next == 99 && !pair[1].ob_state);
    }
    VERIFY(wind_update(END_MCTRL) == 1 && wind_update(END_MCTRL) == 0);
    VERIFY(control_message(control) == 1 && control[0] == 0x7002);
    status = process(0x13a,child,0,0,0);
    VERIFY(status == (LONG)((ULONG)child << 16));
    VERIFY(wind_update(BEG_UPDATE|NO_BLOCK) == 1);
    status = demo(width,height); if (status) return status;
    VERIFY(wind_update(END_UPDATE) == 1 && gem_probe_close(handle) == 0 && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM object drawing: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    return 0;
}
