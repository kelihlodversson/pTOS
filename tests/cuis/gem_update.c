/* GEM update semantics and rectangle coverage across isolated processes. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#define VERIFY(c) do { if (!(c)) return 0x7500 | __LINE__; } while (0)
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static int rectangles(short window, short cover)
{
    UBYTE pixels[20][20] = { { 0 } };
    short x, y, w, h, px, py, cx, cy, cw, ch, count = 0;
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1);
    VERIFY(wind_get(cover, WF_FIRSTXYWH, &cx, &cy, &cw, &ch) == 1);
    VERIFY(cx == 6 && cy == 6 && cw == 8 && ch == 8);
    while (w && h) {
        VERIFY(x >= 1 && y >= 1 && x + w <= 19 && y + h <= 19 && ++count <= 16);
        for (py = y; py < y + h; py++) for (px = x; px < x + w; px++) {
            VERIFY(!pixels[py][px]);
            pixels[py][px] = 1;
        }
        VERIFY(wind_get(window, WF_NEXTXYWH, &x, &y, &w, &h) == 1);
    }
    VERIFY(w == 0 && h == 0 && count > 1);
    for (py = 0; py < 20; py++) for (px = 0; px < 20; px++) {
        int visible = px >= 1 && px < 19 && py >= 1 && py < 19 &&
                      !(px >= 5 && px < 17 && py >= 5 && py < 17);
        VERIFY(pixels[py][px] == visible);
    }
    VERIFY(wind_get(window, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && w > 0 && h > 0);
    return 0;
}
int main(int argc, char **argv, char **envp)
{
    short id, window, cover, message[8], global[15], x, y, w, h;
    LONG child, status;
    int result;
    char tail[3] = { 1, 'c', 0 };
    static const char environment[] = { 0, 0 };
    (void)argv; (void)envp;
    VERIFY(wind_update(BEG_UPDATE) == 0);
    id = appl_init(); VERIFY(id > 0 && mt_appl_init(global) == id);
    VERIFY(wind_update(END_UPDATE) == 0 && wind_update(BEG_MCTRL) == 0);
    if (argc > 1) {
        short parent = appl_find("GEMUPD");
        VERIFY(parent > 0 && parent != id);
        message[0] = 900; message[1] = id;
        message[2] = message[3] = message[4] = message[5] = message[6] = message[7] = 0;
        if (argv[1][0] == 'o') {
            window = wind_create(0, 0, 0, 100, 100);
            VERIFY(window > 0 && wind_open(window, 5, 5, 10, 10) == 1);
            VERIFY(appl_write(parent, 16, message) == 1 && evnt_mesag(message) == 1);
            _Exit(37);
        }
        VERIFY(appl_write(parent, 16, message) == 1);
        VERIFY(wind_update(BEG_UPDATE | NO_BLOCK) == 0);
        message[0] = 901; VERIFY(appl_write(parent, 16, message) == 1);
        /* This mutation must wait for the parent's final END_UPDATE. */
        window = wind_create(0, 0, 0, 100, 100);
        VERIFY(window > 0 && wind_open(window, 30, 40, 50, 60) == 1);
        VERIFY(wind_update(BEG_UPDATE) == 1 && mt_wind_update(BEG_UPDATE | NO_BLOCK, global) == 1);
        message[0] = 902; VERIFY(appl_write(parent, 16, message) == 1);
        _Exit(37); /* Reclaim an open window and both update nesting levels. */
    }
    VERIFY(wind_update(BEG_UPDATE) == 1 && mt_wind_update(BEG_UPDATE | NO_BLOCK, global) == 1);
    VERIFY(evnt_timer(1) == 1); /* GEM permits waits while holding the update semaphore. */
    window = wind_create(0, 0, 0, 100, 100); cover = wind_create(0, 0, 0, 100, 100);
    VERIFY(window > 0 && cover > 0 && wind_open(window, 0, 0, 20, 20) == 1);
    VERIFY(wind_open(cover, 5, 5, 10, 10) == 1);
    result = rectangles(window, cover); if (result) return result;
    VERIFY(wind_set(cover, WF_CURRXYWH, 0, 0, 20, 20) == 1);
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    VERIFY(wind_close(cover) == 1);
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1);
    VERIFY(x == 1 && y == 1 && w == 18 && h == 18);
    VERIFY(wind_get(window, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    VERIFY(wind_get(cover, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    VERIFY(wind_delete(cover) == 1);
    VERIFY(wind_set(window, WF_CURRXYWH, -10, -10, 20, 20) == 1);
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1);
    VERIFY(x == 0 && y == 0 && w == 9 && h == 9);
    VERIFY(wind_set(window, WF_CURRXYWH, 0, 0, 20, 20) == 1);
    child = process(0x4b, 100, (uintptr_t)"C:\\GEMUPD.TOS", (uintptr_t)tail, (uintptr_t)environment);
    VERIFY(child > 0 && appl_yield() == 1);
    VERIFY(evnt_mesag(message) == 1 && message[0] == 900);
    VERIFY(evnt_mesag(message) == 1 && message[0] == 901);
    VERIFY(mt_wind_update(END_UPDATE, global) == 1 && appl_yield() == 1);
    VERIFY(appl_read(APR_NOWAIT, 16, message) == 0);
    VERIFY(wind_update(END_UPDATE) == 1);
    status = process(0x13a, child, 0, 0, 0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 37));
    VERIFY(evnt_mesag(message) == 1 && message[0] == 902);
    VERIFY(wind_update(BEG_UPDATE | NO_BLOCK) == 1);
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && w == 18 && h == 18);
    VERIFY(wind_update(END_UPDATE) == 1);
    tail[1] = 'o';
    child = process(0x4b, 100, (uintptr_t)"C:\\GEMUPD.TOS", (uintptr_t)tail, (uintptr_t)environment);
    VERIFY(child > 0 && evnt_mesag(message) == 1 && message[0] == 900);
    VERIFY(wind_update(BEG_UPDATE) == 1);
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && y == 1 && h == 4);
    VERIFY(appl_write((short)child, 16, message) == 1);
    status = process(0x13a, child, 0, 0, 0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 37));
    /* The existing list survives peer cleanup; FIRST later refreshes it. */
    VERIFY(wind_get(window, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && w && h);
    VERIFY(wind_get(window, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && w && h);
    VERIFY(wind_get(window, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && w && h);
    VERIFY(wind_get(window, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    VERIFY(wind_get(window, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && w == 18 && h == 18);
    VERIFY(appl_exit() == 1); /* Session teardown also releases an explicit lock. */
    VERIFY(appl_init() == id && wind_update(BEG_UPDATE | NO_BLOCK) == 1);
    VERIFY(wind_update(END_UPDATE) == 1 && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM update/regions: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
    }
    return 0;
}
