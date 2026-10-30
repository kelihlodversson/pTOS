/* Standard GEM mouse-control ownership across isolated shared-DSO clients. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#include "control_message.h"
#define VERIFY(c) do { if (!(c)) return 0x7800 | __LINE__; } while (0)
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static int input(short *global)
{
    short x, y, buttons, mods, key, clicks;
    short events = mt_evnt_multi(MU_BUTTON|MU_M1|MU_TIMER,1,0,0,
                                  1,-10,-10,1,1,0,0,0,0,0,0,0,
                                  &x,&y,&buttons,&mods,&key,&clicks,global);
    return events == (MU_BUTTON|MU_M1|MU_TIMER) && clicks == 1 && !key &&
           x >= 0 && y >= 0 && !(buttons & ~3) && !(mods & ~15);
}
int main(int argc, char **argv, char **envp)
{
    short id, global[15], message[8] = { 0 }, window, top, owner, below, spare;
    LONG child, status;
    char tail[3] = { 1, 'c', 0 };
    static const char environment[] = { 0, 0 };
    (void)argv; (void)envp;
    id = appl_init(); VERIFY(id > 0 && mt_appl_init(global) == id);
    if (argc > 1) {
        short parent = appl_find("GEMMCTL");
        VERIFY(parent > 0 && parent != id);
        window = wind_create(0,0,0,300,300);
        VERIFY(window > 0 && wind_open(window,200,200,50,50) == 1);
        VERIFY(wind_update(BEG_MCTRL|NO_BLOCK) == 0);
        message[0] = 900; message[1] = id; message[3] = window;
        VERIFY(appl_write(parent,16,message) == 1);
        VERIFY(control_message(message) == 1 && message[0] == 1);
        message[0] = 901; message[1] = id;
        VERIFY(appl_write(parent,16,message) == 1);
        VERIFY(mt_wind_update(BEG_MCTRL,global) == 1 && input(global));
        message[0] = 902; message[1] = id;
        VERIFY(appl_write(parent,16,message) == 1);
        VERIFY(control_message(message) == 1 && message[0] == 2);
        VERIFY(wind_update(BEG_MCTRL) == 1);
        _Exit(37); /* Authoritative raw exit drops both levels and the window. */
    }
    VERIFY(wind_update(END_MCTRL) == 0);
    VERIFY(wind_update(BEG_MCTRL|NO_BLOCK) == 1 && mt_wind_update(BEG_MCTRL,global) == 1);
    VERIFY(input(global)); /* No window is required to receive modal input. */
    child = process(0x4b,100,(uintptr_t)"C:\\GEMMCTL.TOS",(uintptr_t)tail,(uintptr_t)environment);
    VERIFY(child > 0 && control_message(message) == 1 && message[0] == 900 && message[1] == child);
    VERIFY(wind_get(0,WF_TOP,&top,&owner,&below,&spare) == 1 && top == message[3] && owner == child);
    VERIFY(wind_update(BEG_UPDATE) == 1 && wind_update(END_UPDATE) == 1);
    message[0] = 1; message[1] = id;
    VERIFY(appl_write((short)child,16,message) == 1);
    VERIFY(control_message(message) == 1 && message[0] == 901);
    VERIFY(appl_yield() == 1 && appl_read(APR_NOWAIT,16,message) == 0);
    VERIFY(wind_update(END_MCTRL) == 1);
    VERIFY(appl_yield() == 1 && appl_read(APR_NOWAIT,16,message) == 0);
    VERIFY(wind_update(END_MCTRL) == 1);
    VERIFY(control_message(message) == 1 && message[0] == 902 && message[1] == child);
    VERIFY(wind_update(BEG_MCTRL|NO_BLOCK) == 0);
    message[0] = 2; message[1] = id;
    VERIFY(appl_write((short)child,16,message) == 1 && wind_update(BEG_MCTRL) == 1);
    status = process(0x13a,child,0,0,0);
    VERIFY(status == (LONG)(((ULONG)child << 16) | 37));
    VERIFY(wind_update(BEG_MCTRL) == 1 && appl_exit() == 1);
    VERIFY(appl_init() == id && wind_update(BEG_MCTRL|NO_BLOCK) == 1);
    VERIFY(wind_update(END_MCTRL) == 1 && wind_update(END_MCTRL) == 0 && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM mouse control: PASS\r\n";
        ptos_write(1,marker,sizeof(marker)-1);
    }
    return 0;
}
