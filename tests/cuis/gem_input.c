/* Normal GEM source: exercise all input masks through the shared DSO. */
#include <gem.h>
#include "ptos.h"
#define VERIFY(c) do { if (!(c)) return 0x7700 | __LINE__; } while (0)
int main(void)
{
    short id, global[15], window, x, y, buttons, mods, key, clicks, message[8];
    short rate, events;
    id = appl_init(); VERIFY(id > 0 && mt_appl_init(global) == id);
    window = wind_create(0, 0, 0, 32767, 32767);
    VERIFY(window > 0 && wind_open(window, 0, 0, 32767, 32767) == 1);
    VERIFY(graf_mkstate(&x, &y, &buttons, &mods) == 1);
    VERIFY(evnt_button(1, 0, 0, &x, &y, &buttons, &mods) == 1);
    VERIFY(mt_evnt_button(1, 0, 0, &x, &y, &buttons, &mods, global) == 1);
    VERIFY(evnt_mouse(0, 0, 0, 32767, 32767, &x, &y, &buttons, &mods) == 1);
    VERIFY(mt_evnt_mouse(1, -10, -10, 1, 1, &x, &y, &buttons, &mods, global) == 1);
    for (rate = 0; rate < 5; rate++) {
        VERIFY(evnt_dclick(rate, 1) == rate && mt_evnt_dclick(0, 0, global) == rate);
    }
    VERIFY(mt_evnt_dclick(0, 1, global) == 0);
    key = clicks = -1;
    events = evnt_multi(MU_KEYBD|MU_BUTTON|MU_M1|MU_M2|MU_MESAG|MU_TIMER,
                        1, 0, 0, 0, 0, 0, 32767, 32767, 1, -10, -10, 1, 1,
                        message, 0, &x, &y, &buttons, &mods, &key, &clicks);
    VERIFY(events == (MU_BUTTON|MU_M1|MU_M2|MU_MESAG|MU_TIMER));
    VERIFY(message[0] == WM_REDRAW && message[3] == window && !key && clicks == 1);
    VERIFY(x >= 0 && y >= 0 && !(buttons & ~3) && !(mods & ~15));
    VERIFY(mt_evnt_multi(MU_KEYBD|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
                         0,5,&x,&y,&buttons,&mods,&key,&clicks,global) == MU_TIMER);
    VERIFY(!key && !clicks && wind_delete(window) == 1 && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM input events: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
    }
    return 0;
}
