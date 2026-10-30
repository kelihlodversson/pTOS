/* Standard GEM messages and globally visible stack metadata in isolated apps. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#define VERIFY(c) do { if (!(c)) return 0x7600 | __LINE__; } while (0)
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static int control(short message[8])
{
    short events;
    do {
        events = evnt_multi(MU_MESAG | MU_TIMER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             message, 2000, 0, 0, 0, 0, 0, 0);
        if (!(events & MU_MESAG)) return 0;
        if (message[0] == WM_REDRAW && (message[1] || message[2] || message[6] < 0 || message[7] < 0)) return 0;
    } while (message[0] == WM_REDRAW);
    return 1;
}
static int command(short child, short code, short message[8])
{
    short i;
    for (i = 0; i < 8; i++) message[i] = 0;
    message[0] = code; message[1] = gl_apid;
    if (!appl_write(child, 16, message) || !control(message)) return 0;
    return message[0] == code + 900;
}
int main(int argc, char **argv, char **envp)
{
    short id, a, b, message[8] = { 0 }, global[15], top, owner, below, spare;
    short foreign;
    LONG child, status;
    char tail[3] = { 1, 'c', 0 };
    static const char environment[] = { 0, 0 };
    (void)argv; (void)envp;
    VERIFY(wind_find(0,0) == -1 && mt_wind_find(0,0,NULL) == -1);
    id = appl_init(); VERIFY(id > 0 && mt_appl_init(global) == id);
    if (argc > 1) {
        short parent = appl_find("GEMFOC");
        VERIFY(parent > 0 && parent != id);
        a = wind_create(0, 0, 0, 300, 300);
        VERIFY(a > 0 && wind_open(a, 100, 100, 60, 60) == 1);
        message[0] = 900; message[1] = id; message[3] = a;
        VERIFY(appl_write(parent, 16, message) == 1);
        for (;;) {
            short code;
            VERIFY(control(message)); code = message[0];
            if (code == 5) _Exit(37);
            if (code == 1) VERIFY(wind_set(a, WF_BOTTOM, 0, 0, 0, 0) == 1);
            else if (code == 2) VERIFY(mt_wind_set(a, WF_TOP, 0, 0, 0, 0, global) == 1);
            else if (code == 3) VERIFY(wind_close(a) == 1);
            else if (code == 4) VERIFY(wind_open(a, 100, 100, 60, 60) == 1);
            else return 99;
            message[0] = code + 900; message[1] = id;
            VERIFY(appl_write(parent, 16, message) == 1);
        }
    }
    VERIFY(wind_get(0, WF_TOP, &top, &owner, &below, &spare) == 1 && !top && !owner && !below);
    VERIFY(wind_find(0,0) == 0 && wind_find(-32768,32767) == 0);
    a = wind_create(0, 0, 0, 300, 300); b = wind_create(0, 0, 0, 300, 300);
    VERIFY(a > 0 && b > 0 && wind_open(a, 0, 0, 80, 80) == 1);
    VERIFY(wind_find(0,0) == a && mt_wind_find(79,79,global) == a);
    VERIFY(wind_find(80,79) == 0 && wind_find(79,80) == 0 && wind_find(80,80) == 0);
    VERIFY(wind_find(-1,0) == 0 && wind_find(0,-1) == 0);
    VERIFY(evnt_mesag(message) == 1 && message[0] == WM_REDRAW && !message[1] && !message[2]);
    VERIFY(message[3] == a && message[4] == 1 && message[5] == 1 && message[6] == 78 && message[7] == 78);
    VERIFY(wind_open(b, 50, 50, 80, 80) == 1);
    VERIFY(wind_find(49,50) == a && wind_find(50,50) == b && wind_find(129,129) == b);
    VERIFY(wind_find(130,129) == 0 && wind_find(129,130) == 0 && wind_find(131,131) == 0);
    VERIFY(mt_wind_get(0, WF_TOP, &top, &owner, &below, &spare, global) == 1 && top == b && owner == id && below == a);
    child = process(0x4b, 100, (uintptr_t)"C:\\GEMFOC.TOS", (uintptr_t)tail, (uintptr_t)environment);
    VERIFY(child > 0 && control(message) && message[0] == 900); foreign = message[3];
    VERIFY(foreign > 0 && foreign != a && foreign != b);
    VERIFY(wind_find(110,110) == foreign && mt_wind_find(159,159,global) == foreign);
    VERIFY(wind_find(160,159) == 0 && wind_find(159,160) == 0);
    VERIFY(wind_get(0, WF_TOP, &top, &owner, &below, &spare) == 1 && top == foreign && owner == child && below == b);
    VERIFY(wind_get(0, WF_BOTTOM, &top, &owner, &below, &spare) == 1 && top == a);
    VERIFY(wind_get(b, WF_OWNER, &top, &owner, &below, &spare) == 1 && top == id && owner == 1 && below == foreign && spare == a);
    VERIFY(wind_set(foreign, WF_TOP, 0, 0, 0, 0) == 0);
    VERIFY(command((short)child, 1, message));
    VERIFY(wind_find(110,110) == b && wind_find(140,140) == foreign);
    VERIFY(wind_get(0, WF_TOP, &top, &owner, &below, &spare) == 1 && top == b && owner == id && below == a);
    VERIFY(wind_set(a, WF_TOP, 0, 0, 0, 0) == 1);
    VERIFY(wind_get(0, WF_TOP, &top, &owner, &below, &spare) == 1 && top == a && below == b);
    VERIFY(command((short)child, 2, message));
    VERIFY(wind_find(110,110) == foreign);
    VERIFY(wind_get(0, WF_TOP, &top, &owner, &below, &spare) == 1 && top == foreign && owner == child);
    VERIFY(command((short)child, 3, message));
    VERIFY(wind_find(110,110) == b && wind_find(140,140) == 0);
    VERIFY(wind_get(foreign, WF_OWNER, &top, &owner, &below, &spare) == 1 && top == child && !owner && !below && !spare);
    VERIFY(wind_get(0, WF_TOP, &top, &owner, &below, &spare) == 1 && top == a);
    VERIFY(command((short)child, 4, message));
    VERIFY(wind_find(110,110) == foreign);
    message[0] = 5; message[1] = id;
    VERIFY(appl_write((short)child, 16, message) == 1);
    status = process(0x13a, child, 0, 0, 0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 37));
    VERIFY(wind_get(0, WF_TOP, &top, &owner, &below, &spare) == 1 && top == a && owner == id);
    VERIFY(wind_get(foreign, WF_OWNER, &top, &owner, &below, &spare) == 0);
    VERIFY(wind_find(110,110) == b && wind_find(140,140) == 0);
    VERIFY(wind_delete(a) == 1 && wind_delete(b) == 1 && appl_exit() == 1);
    VERIFY(wind_find(0,0) == -1);
    {
        static const char marker[] = "x32 GEM window messages/focus: PASS\r\n";
        static const char find_marker[] = "x32 GEM window lookup: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
        ptos_write(1, find_marker, sizeof(find_marker)-1);
    }
    return 0;
}
