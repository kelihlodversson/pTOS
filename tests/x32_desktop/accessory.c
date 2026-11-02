/* An ordinary GEM application alongside the resident libaes EmuDesk. */
#include <stddef.h>
#include <gem.h>
#include "ptos.h"
#include "vdipb.h"
#define VERIFY(c) do { if (!(c)) { record("FAIL",__LINE__,0); _Exit(99); } } while (0)
static void record(const char *event, unsigned number, unsigned window)
{
    char text[100], digits[12];
    unsigned used = 0, n, at;
    const char *prefix = "x32 libaes accessory ";
    while (*prefix) text[used++] = *prefix++;
    while (*event) text[used++] = *event++;
    for (at = 0; at < 2; at++) {
        text[used++] = ' '; n = 0;
        do { digits[n++] = '0'+number%10; number /= 10; } while (number);
        while (n) text[used++] = digits[--n];
        number = window;
    }
    text[used++] = '\r'; text[used++] = '\n';
    for (at = 0; at < used; at++) {
        register unsigned long long ax __asm__("rax") = (13ULL << 32) | 3;
        __asm__ volatile ("syscall" : "+a"(ax) : "D"(1ULL),"S"((unsigned long long)text[at])
                          : "rcx","r11","memory","cc");
    }
}
static VDICONTROL control;
static WORD input[16], points[8], output[64], coordinates[16];
static VDIPB pb = { &control,input,points,output,coordinates };
static void draw(short window, short handle)
{
    short x, y, w, h, wx, wy, ww, wh;
    VERIFY(wind_update(BEG_UPDATE));
    VERIFY(wind_get(window,WF_WORKXYWH,&wx,&wy,&ww,&wh));
    VERIFY(wind_get(window,WF_FIRSTXYWH,&x,&y,&w,&h));
    while (w && h) {
        control.handle = handle; control.code = 129; control.nptsin = 2; control.nintin = 1;
        points[0] = x; points[1] = y; points[2] = x+w-1; points[3] = y+h-1; input[0] = 1;
        VERIFY(vdi(&pb) == 0);
        control.code = 23; control.nptsin = 0; input[0] = 1; VERIFY(vdi(&pb) == 0);
        control.code = 25; input[0] = 0; VERIFY(vdi(&pb) == 0);
        control.code = 11; control.subcode = 1; control.nptsin = 2; control.nintin = 0;
        points[0] = wx; points[1] = wy; points[2] = wx+ww-1; points[3] = wy+wh-1;
        VERIFY(vdi(&pb) == 0);
        control.subcode = 0;
        VERIFY(wind_get(window,WF_NEXTXYWH,&x,&y,&w,&h));
    }
    VERIFY(wind_update(END_UPDATE));
}
extern short _app;
static short preinit_app = -1, constructor_app = -1;
static unsigned startup_order;
static void accessory_preinit(void)
{
    preinit_app = _app;
    startup_order = 1;
}
__attribute__((used,section(".preinit_array")))
static void (*const preinitializer)(void) = accessory_preinit;
__attribute__((constructor)) static void accessory_init(void)
{
    constructor_app = _app;
    startup_order = startup_order == 1 ? 2 : 3;
}
int main(void)
{
    short me, menu, window = -1, message[8], events, key, handle, i;
    VERIFY(startup_order == 2 && preinit_app == 0 && constructor_app == 0 && _app == 0);
    VERIFY((me = appl_init()) > 0 && appl_find("EMUDESK") > 0);
    VERIFY(shel_write(SWM_NEWMSG,NM_APTERM,0,NULL,NULL) == 1);
    menu = menu_register(me,"  Resident accessory");
    VERIFY(menu >= 0);
    control.code = 100; control.nintin = 11;
    for (i = 0; i < 10; i++) input[i] = 1;
    input[10] = 2;
    VERIFY(vdi(&pb) == 0); handle = control.handle;
    record("READY",me,menu);
    for (;;) {
        events = evnt_multi(MU_KEYBD|MU_MESAG,0,0,0,0,0,0,0,0,0,0,0,0,0,
                            message,0,NULL,NULL,NULL,NULL,&key,NULL);
        VERIFY(events);
        if (events & MU_MESAG) {
            if (message[0] == AC_CLOSE) {
                short kind;
                VERIFY(message[3] == menu && (window < 0 || !wind_get(window,WF_KIND,&kind,NULL,NULL,NULL)));
                window = -1; record("CLOSE",me,menu);
            }
            if (message[0] == AP_TERM) break;
            if (message[0] == AC_OPEN) {
                VERIFY(message[4] == menu && message[1] > 0 && !message[2]);
                if (window < 0) {
                    window = wind_create(NAME|CLOSER|MOVER,580,490,360,200);
                    VERIFY(window > 0 && wind_set_str(window,WF_NAME,"Resident desk accessory"));
                    VERIFY(wind_open(window,580,490,360,200));
                } else VERIFY(wind_set(window,WF_TOP,0,0,0,0));
                record("OPEN",me,window);
            }
            if (message[0] == WM_REDRAW && window > 0) draw(window,handle);
            if (message[0] == WM_TOPPED && window > 0) VERIFY(wind_set(window,WF_TOP,0,0,0,0));
            if (message[0] == WM_CLOSED && window > 0) {
                VERIFY(wind_close(window) && wind_delete(window)); window = -1;
                record("CLOSED",me,menu);
            }
        }
        if ((events & MU_KEYBD) && (key & 255) == 'q') break;
    }
    if (window > 0) VERIFY(wind_close(window) && wind_delete(window));
    control.code = 101; control.nintin = control.nptsin = 0; VERIFY(vdi(&pb) == 0);
    VERIFY(appl_exit());
    record("EXIT",me,37);
    return 37;
}
