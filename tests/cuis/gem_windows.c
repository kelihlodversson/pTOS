/* Standard window metadata calls in isolated clients, using GEM headers. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#include "window_api.c"
#define VERIFY(c) do { if (!(c)) return 0x7400 | __LINE__; } while (0)
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
#include "control_message.h"
static int headless_window_checks(short *global)
{
    short window, x = 99,y = 99,w = 99,h = 99;
    VERIFY(mt_graf_handle(&x,&y,&w,&h,global) == 0 && x == 99 && y == 99 && w == 99 && h == 99);
    VERIFY(graf_mouse(ARROW,NULL) == 0 && graf_mouse(M_OFF,NULL) == 0);
    VERIFY(wind_create(NAME,0,0,100,80) == -1);
    window = wind_create(0,10,20,100,80);
    VERIFY(window > 0 && wind_open(window,10,20,100,80) == 1);
    VERIFY(wind_get(window,WF_WORKXYWH,&x,&y,&w,&h) == 1 && x == 11 && y == 21 && w == 98 && h == 78);
    VERIFY(wind_set(window,WF_CURRXYWH,20,30,120,90) == 1);
    VERIFY(wind_get(window,WF_PREVXYWH,&x,&y,&w,&h) == 1 && x == 10 && y == 20 && w == 100 && h == 80);
    VERIFY(wind_close(window) == 1 && wind_delete(window) == 1);
    return 0;
}
int main(int argc, char **argv, char **envp)
{
    short id, window, message[8], globals[15], x = 99, y, w, h;
    int result;
    BOOL graphics;
    LONG child, status;
    char tail[3] = { 1, 'c', 0 };
    static const char environment[] = { 0, 0 };
    (void)argv; (void)envp;
    VERIFY(wind_create(0, 0, 0, 10, 10) == -1 && wind_get(1, WF_KIND, &x, 0, 0, 0) == 0 && x == 99);
    id = appl_init(); VERIFY(id > 0 && mt_appl_init(globals) == id);
    graphics = graf_handle(NULL,NULL,NULL,NULL) > 0;
    if (argc > 1) {
        short parent = appl_find("GEMWIN");
        VERIFY(parent > 0 && parent != id);
        message[0] = 899; message[1] = id;
        message[2] = message[3] = message[4] = message[5] = message[6] = message[7] = 0;
        VERIFY(appl_write(parent, 16, message) == 1 && control_message(message) == 1);
        VERIFY(wind_get(message[3], WF_CURRXYWH, &x, &y, &w, &h) == 0);
        window = wind_create(graphics ? NAME : 0, 0, 0, 100, 100);
        VERIFY(window > 0 && window != message[3] && wind_open(window, 5, 6, 30, 40) == 1);
        message[1] = id; message[3] = window;
        VERIFY(appl_write(parent, 16, message) == 1 && control_message(message) == 1);
        _Exit(37); /* Kernel cleanup must reclaim the open window. */
    }
    result = graphics ? gem_window_checks(globals) : headless_window_checks(globals); if (result) return result;
    window = wind_create(graphics ? NAME|INFO : 0, 0, 0, 200, 160);
    VERIFY(window > 0 && wind_set_str(window,WF_NAME,"Parent") == 1);
    VERIFY(wind_set_str(window,WF_INFO,"Shared native frame") == 1);
    VERIFY(wind_open(window,10,20,100,80) == 1);
    VERIFY(appl_init() == id && wind_get(window, WF_CURRXYWH, &x, &y, &w, &h) == 1);
    child = process(0x4b, 100, (uintptr_t)"C:\\GEMWIN.TOS", (uintptr_t)tail, (uintptr_t)environment);
    VERIFY(child > 0 && control_message(message) == 1 && message[0] == 899);
    message[0] = 900; message[1] = id; message[2] = 0; message[3] = window;
    message[4] = message[5] = message[6] = message[7] = 0;
    VERIFY(appl_write((short)child, 16, message) == 1 && control_message(message) == 1);
    VERIFY(message[3] != window && wind_delete(message[3]) == 0);
    VERIFY(appl_write((short)child, 16, message) == 1);
    status = process(0x13a, child, 0, 0, 0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 37));
    VERIFY(wind_get(window, WF_CURRXYWH, &x, &y, &w, &h) == 1 && x == 10 && y == 20);
    VERIFY(mt_appl_exit(globals) == 1 && wind_delete(window) == 0);
    VERIFY(appl_init() == id && wind_get(window, WF_KIND, &x, &y, &w, &h) == 0);
    VERIFY(appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM window lifecycle: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
    }
    return 0;
}
