/* Standard GEM source, real VDI pixel checks, and a userspace object demo. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#include "gem_draw_probe.h"
#define VERIFY(c) do { if (!(c)) return 0x7900 | __LINE__; } while (0)
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static void object(OBJECT *o, short type, short x, short y, short w, short h)
{
    o->ob_next = o->ob_head = o->ob_tail = -1;
    o->ob_type = type; o->ob_flags = o->ob_state = 0;
    o->ob_x = x; o->ob_y = y; o->ob_width = w; o->ob_height = h;
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
    {
        static const char ready[] = "x32 GEM object screenshot: READY\r\n";
        ptos_write(1,ready,sizeof(ready)-1);
    }
    /* Keep the serial test marker out of the visual framebuffer. */
    VERIFY(objc_draw(scene,0,0,0,height-32,width,32) == 1);
    VERIFY(evnt_timer(1800) == 1);
    return 0;
}
int main(int argc, char **argv, char **envp)
{
    OBJECT box = { 0 };
    USERBLK user;
    WORD handle, width, height, pixels[400];
    short x,y,global[15];
    LONG child, status;
    char tail[3] = { 1,'f',0 };
    static const char environment[] = { 0,0 };
    (void)argv; (void)envp;
    VERIFY(appl_init() > 0 && mt_appl_init(global) > 0);
    object(&box,G_BOX,0,0,100,100); box.ob_spec.index = 0x1170;
    if (argc > 1) {
        box.ob_type = G_USERDEF; box.ob_spec.userblk = &user;
        user.ub_code = fault_callback; user.ub_parm = 0;
        (void)objc_draw(&box,0,0,0,0,100,100);
        return 99;
    }
    if (gem_probe_open(&handle,&width,&height) != 0) {
        VERIFY(objc_draw(&box,0,0,0,0,100,100) == 0 && appl_exit() == 1);
        {
            static const char marker[] = "x32 GEM object drawing: unavailable\r\n";
            ptos_write(1,marker,sizeof(marker)-1);
        }
        return 0;
    }
    VERIFY(wind_update(BEG_UPDATE) == 1);
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1 && gem_probe_pixel(handle,26,26) == 0);
    object(&box,G_BOX,20,20,20,20); box.ob_spec.index = 0x1172;
    VERIFY(mt_objc_draw(&box,0,0,25,25,10,10,global) == 1);
    VERIFY(gem_probe_pixel(handle,26,26) == 2 && gem_probe_pixel(handle,24,24) == 0 && gem_probe_pixel(handle,35,35) == 0);
    for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) pixels[y*20+x] = gem_probe_pixel(handle,x+20,y+20);
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 1 && gem_probe_pixel(handle,26,26) != 2);
    VERIFY(mt_objc_change(&box,0,0,0,0,100,100,0,1,global) == 1);
    for (y = 0; y < 20; y++) for (x = 0; x < 20; x++) VERIFY(gem_probe_pixel(handle,x+20,y+20) == pixels[y*20+x]);
    box.ob_type = G_USERDEF; box.ob_spec.userblk = &user;
    user.ub_code = callback; user.ub_parm = 123;
    VERIFY(objc_draw(&box,0,0,20,20,10,10) == 1 && gem_probe_pixel(handle,23,23) == 3);
    VERIFY(wind_update(END_UPDATE) == 1);
    child = process(0x4b,100,(uintptr_t)"C:\\GEMDRAW.TOS",(uintptr_t)tail,(uintptr_t)environment);
    VERIFY(child > 0);
    status = process(0x13a,child,0,0,0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 0x0b00));
    VERIFY(wind_update(BEG_UPDATE|NO_BLOCK) == 1);
    status = demo(width,height); if (status) return status;
    VERIFY(wind_update(END_UPDATE) == 1 && gem_probe_close(handle) == 0 && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM object drawing: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    return 0;
}
