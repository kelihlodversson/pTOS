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
static short control_message(short message[8])
{
    short result;
    do { result = evnt_mesag(message); } while (result && message[0] == WM_REDRAW);
    return result;
}
int main(int argc, char **argv, char **envp)
{
    short id, window, message[8], globals[15], x = 99, y, w, h;
    int result;
    LONG child, status;
    char tail[3] = { 1, 'c', 0 };
    static const char environment[] = { 0, 0 };
    (void)argv; (void)envp;
    VERIFY(wind_create(0, 0, 0, 10, 10) == -1 && wind_get(1, WF_KIND, &x, 0, 0, 0) == 0 && x == 99);
    id = appl_init(); VERIFY(id > 0 && mt_appl_init(globals) == id);
    if (argc > 1) {
        short parent = appl_find("GEMWIN");
        VERIFY(parent > 0 && parent != id);
        message[0] = 899; message[1] = id;
        message[2] = message[3] = message[4] = message[5] = message[6] = message[7] = 0;
        VERIFY(appl_write(parent, 16, message) == 1 && control_message(message) == 1);
        VERIFY(wind_get(message[3], WF_CURRXYWH, &x, &y, &w, &h) == 0);
        window = wind_create(0, 0, 0, 100, 100);
        VERIFY(window > 0 && window != message[3] && wind_open(window, 5, 6, 30, 40) == 1);
        message[1] = id; message[3] = window;
        VERIFY(appl_write(parent, 16, message) == 1 && control_message(message) == 1);
        _Exit(37); /* Kernel cleanup must reclaim the open window. */
    }
    result = gem_window_checks(globals); if (result) return result;
    window = wind_create(0, 0, 0, 200, 160);
    VERIFY(window > 0 && wind_open(window, 10, 20, 100, 80) == 1);
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
