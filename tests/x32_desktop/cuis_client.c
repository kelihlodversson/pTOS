/* An ordinary GEM application alongside the resident libaes EmuDesk. */
#include <stddef.h>
#include <gem.h>
#include "ptos.h"
#include "vdipb.h"
extern short _app;
#define VERIFY(c) do { if (!(c)) { record("FAIL",__LINE__,0); _Exit(99); } } while (0)
static void record(const char *event, unsigned number, unsigned window)
{
    char text[100], digits[12];
    unsigned used = 0, n, at;
    const char *prefix = "x32 desktop client ";
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
int main(int argc, char **argv)
{
    short me, window, message[8], events, key, handle, i;
    VERIFY(_app == 1);
    VERIFY((me = appl_init()) > 0 && appl_find("EMUDESK") > 0);
    if (argc > 1) {
        const char *expected = "param", *actual = argv[1];
        VERIFY(argc == 2);
        while (*expected && *actual == *expected) { actual++; expected++; }
        VERIFY(!*expected && !*actual);
        record("PARAMETERS PASS",me,0);
    }
    control.code = 100; control.nintin = 11;
    for (i = 0; i < 10; i++) input[i] = 1;
    input[10] = 2;
    VERIFY(vdi(&pb) == 0); handle = control.handle;
    window = wind_create(NAME|CLOSER|MOVER,600,100,360,220);
    VERIFY(window > 0 && wind_set_str(window,WF_NAME,"Resident GEM application"));
    VERIFY(wind_open(window,600,100+(me&1)*280,360,220));
    record("READY",me,window);
    for (;;) {
        events = evnt_multi(MU_KEYBD|MU_MESAG,0,0,0,0,0,0,0,0,0,0,0,0,0,
                            message,0,NULL,NULL,NULL,NULL,&key,NULL);
        VERIFY(events);
        if ((events & MU_MESAG) && message[0] == WM_REDRAW) draw(window,handle);
        if ((events & MU_MESAG) && message[0] == WM_TOPPED) VERIFY(wind_set(window,WF_TOP,0,0,0,0));
        if ((events & MU_MESAG) && message[0] == WM_CLOSED) break;
        if (events & MU_KEYBD) {
            if ((key & 255) == 'q') break;
            if ((key & 255) == 'f') {
                record("fault READY",me,window);
                __asm__ volatile ("ud2");
            }
        }
    }
    VERIFY(wind_close(window) && wind_delete(window));
    control.code = 101; control.nintin = control.nptsin = 0; VERIFY(vdi(&pb) == 0);
    VERIFY(appl_exit());
    record("EXIT",me,37);
    return 37;
}
