/* Standard GEM-only discovery, activation, prepare/abort/refusal/completion. */
#include <gem.h>
#include "ptos.h"
#include "gem_lifecycle.h"
#define VERIFY(c) do { if (!(c)) { report(__LINE__); return 0x6800 | __LINE__; } } while (0)
static void report(unsigned line)
{
    char text[48] = "x32 GEM lifecycle failure ", reverse[12];
    unsigned used = 26, n = 0;
    do { reverse[n++] = '0'+line%10; line /= 10; } while (line);
    while (n) text[used++] = reverse[--n];
    text[used++] = '\r'; text[used++] = '\n';
    ptos_write(2,text,used);
}
static short launch(char role, int acc)
{
    char tail[128] = { 0 }, reverse[12];
    unsigned pid = aes_global[2], used = 3, n = 0;
    tail[1] = role; tail[2] = ' ';
    do { reverse[n++] = '0'+pid%10; pid /= 10; } while (pid);
    while (n) tail[used++] = reverse[--n];
    tail[0] = used-1;
    return shel_write(SWM_LAUNCH,GEMAPP,CL_NORMAL,acc ? "C:\\GEMLIFE.ACC" : "C:\\GEMLIFE.PRG",tail);
}
static int receive(short message[8])
{
    return evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,10000,
                      0,0,0,0,0,0) & MU_MESAG;
}
static int ready(short pid, short result[8])
{
    unsigned tries;
    for (tries = 0; tries < 64; tries++) {
        if (!receive(result)) return 0;
        if (result[0] == CH_EXIT && result[3] == pid) { report((unsigned short)result[4]); return 0; }
        if (result[0] == LIFE_READY && result[3] == pid) return 1;
    }
    return 0;
}
static int stop(short pid)
{
    short message[8] = { LIFE_STOP,0,0,0,0,0,0,0 };
    unsigned tries;
    if (!appl_write(pid,16,message)) return 0;
    for (tries = 0; tries < 64; tries++) {
        if (!receive(message)) return 0;
        if (message[0] == CH_EXIT && message[3] == pid) return message[4] == 37;
    }
    return 0;
}
int gem_lifecycle_checks(void)
{
    short app, acc, a[8], b[8], message[8], type, id, top;
    int mode, closed, exited, completed, seen, opened;
    unsigned tries;
    char name[9];
    app = launch('u',0); VERIFY(app > 0);
    VERIFY(ready(app,a));
    VERIFY(shel_write(SWM_REZCHANGE,2,0,0,0) == 0);
    VERIFY(shel_write(SWM_REZCHANGE,0,1,0,0) == 0);
    VERIFY(shel_write(SWM_SHUTDOWN,0,SD_COMPLETE,0,0) == 0); /* No AP_TERM consent. */
    VERIFY(appl_control(app,APC_TOP,0) == 1);
    VERIFY(wind_get(0,WF_TOP,&top,0,0,0) && top == a[5]);
    VERIFY(appl_control(-1,APC_INFO,&type) == 1);
    VERIFY(stop(app));
    for (mode = SD_PARTIAL; mode <= SD_COMPLETE; mode++) {
        app = launch('y',0); VERIFY(app > 0 && ready(app,a));
        acc = launch('a',1); VERIFY(acc > 0 && ready(acc,b));
        seen = 0;
        if (appl_search(APP_FIRST,name,&type,&id)) do {
            if (id == app) { VERIFY(type == APP_APPLICATION); seen |= 1; }
            if (id == acc) { VERIFY(type == APP_ACCESSORY); seen |= 2; }
        } while (appl_search(APP_NEXT,name,&type,&id));
        VERIFY(seen == 3);
        /* Queue a new opening before the lifecycle packet. Its window must
         * also be gone when AC_CLOSE reaches the accessory. */
        for (tries = 0; tries < 8; tries++) message[tries] = 0;
        message[0] = AC_OPEN; message[4] = b[4];
        VERIFY(appl_write(acc,16,message));
        VERIFY(shel_write(SWM_SHUTDOWN,0,mode,0,0) == 1);
        closed = exited = completed = opened = 0;
        for (tries = 0; tries < 96 && (!completed || exited != (mode == SD_COMPLETE ? 3 : 1) || closed != 2); tries++) {
            VERIFY(receive(message));
            if (message[0] == LIFE_OPENED) { VERIFY(!closed); opened++; }
            if (message[0] == LIFE_CLOSED) {
                VERIFY(message[1] == acc && (message[3] == b[4] || message[3] == b[7]) && !message[4]);
                closed++;
            }
            if (message[0] == CH_EXIT) {
                VERIFY(message[4] == 37);
                if (message[3] == app) exited |= 1;
                if (message[3] == acc) exited |= 2;
            }
            if (message[0] == SHUT_COMPLETED) completed++;
            VERIFY(message[0] != AP_TFAIL);
        }
        VERIFY(opened == 1 && closed == 2 && completed == 1 && exited == (mode == SD_COMPLETE ? 3 : 1));
        VERIFY(shel_write(SWM_SHUTDOWN,0,SD_ABORT,0,0) == 1);
        if (mode == SD_PARTIAL) VERIFY(stop(acc));
    }
    app = launch('n',0); VERIFY(app > 0 && ready(app,a));
    VERIFY(shel_write(SWM_SHUTDOWN,0,SD_COMPLETE,0,0) == 1);
    completed = 0;
    for (tries = 0; tries < 64; tries++) {
        VERIFY(receive(message));
        VERIFY(message[0] != SHUT_COMPLETED && message[0] != CH_EXIT);
        if (message[0] == AP_TFAIL) { VERIFY(message[1] == 23); completed = 1; break; }
    }
    VERIFY(completed && stop(app));
    app = launch('y',0); VERIFY(app > 0 && ready(app,a));
    VERIFY(shel_write(SWM_SHUTDOWN,0,SD_COMPLETE,0,0) == 1);
    VERIFY(shel_write(SWM_SHUTDOWN,0,SD_ABORT,0,0) == 1);
    VERIFY(appl_control(app,APC_TOP,0) == 1 && stop(app));
    {
        static const char pass[] = "x32 GEM discovery/control/shutdown: PASS\r\n";
        ptos_write(1,pass,sizeof(pass)-1);
    }
    return 0;
}
