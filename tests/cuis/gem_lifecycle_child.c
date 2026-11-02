/* Ordinary GEM participants: consenting, refusing and unaware applications,
 * plus an accessory with multiple registered Desk entries. */
#include <gem.h>
#include "gem_lifecycle.h"
#define VERIFY(c) do { if (!(c)) return 0x6900 | __LINE__; } while (0)
extern short _app;
int main(int argc, char **argv)
{
    short me, parent = 0, menus[2] = { -1,-1 }, windows[3] = { -1,-1,-1 }, message[8], i, kind;
    char role, *digit;
    VERIFY(argc == 3);
    role = argv[1][0]; digit = argv[2];
    while (*digit >= '0' && *digit <= '9') parent = parent*10+*digit++-'0';
    VERIFY(parent > 0 && !*digit && (me = appl_init()) > 0);
    if (role != 'u') VERIFY(shel_write(SWM_NEWMSG,NM_APTERM,0,0,0) == 1);
    if (!_app) {
        menus[0] = menu_register(me,"  Lifecycle A");
        menus[1] = menu_register(me,"  Lifecycle B");
        VERIFY(menus[0] >= 0 && menus[1] >= 0);
    }
    for (i = 0; i < 2; i++) {
        windows[i] = wind_create(0,50+i*30,50+i*20,120,80);
        VERIFY(windows[i] > 0 && wind_open(windows[i],50+i*30,50+i*20,120,80));
    }
    message[0] = LIFE_READY; message[1] = me; message[2] = 0;
    message[3] = me; message[4] = menus[0]; message[5] = windows[1];
    message[6] = windows[0]; message[7] = menus[1];
    VERIFY(appl_write(parent,16,message) == 1);
    for (;;) {
        VERIFY(evnt_mesag(message) == 1);
        if (message[0] == LIFE_STOP) break;
        if (message[0] == AC_OPEN) {
            VERIFY(!_app && windows[2] < 0);
            windows[2] = wind_create(0,150,150,120,80);
            VERIFY(windows[2] > 0 && wind_open(windows[2],150,150,120,80));
            message[0] = LIFE_OPENED; message[1] = me; message[2] = 0;
            VERIFY(appl_write(parent,16,message));
        }
        if (message[0] == AC_CLOSE) {
            VERIFY(!_app && (message[3] == menus[0] || message[3] == menus[1]));
            for (i = 0; i < 3; i++) {
                VERIFY(windows[i] < 0 || !wind_get(windows[i],WF_KIND,&kind,0,0,0));
                windows[i] = -1;
            }
            message[0] = LIFE_CLOSED; message[1] = me; message[2] = 0;
            message[4] = shel_write(SWM_SHUTDOWN,0,SD_ABORT,0,0);
            VERIFY(appl_write(parent,16,message) == 1);
        }
        if (message[0] == AP_TERM) {
            VERIFY(role != 'u' && message[1] == -1 && message[5] == AP_TERM);
            if (role != 'n') break;
            for (i = 0; i < 8; i++) message[i] = 0;
            message[0] = AP_TFAIL; message[1] = 23;
            VERIFY(shel_write(SWM_AESMSG,0,0,(char *)message,0) == 1);
        }
    }
    for (i = 0; i < 3; i++) if (windows[i] > 0)
        VERIFY(wind_close(windows[i]) && wind_delete(windows[i]));
    VERIFY(appl_exit() == 1);
    return 37;
}
