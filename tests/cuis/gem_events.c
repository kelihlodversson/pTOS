/* Standard GEM combined waits across ordinary isolated dynamic processes. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#define VERIFY(c) do { if (!(c)) return 0x7200 | __LINE__; } while (0)
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static short multi(short mask, unsigned long interval, short *message, short state[6], short *global)
{
    if (global)
        return mt_evnt_multi(mask, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, message, interval,
                             &state[0], &state[1], &state[2], &state[3], &state[4], &state[5], global);
    return evnt_multi(mask, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, message, interval,
                      &state[0], &state[1], &state[2], &state[3], &state[4], &state[5]);
}
static BOOL state_valid(const short state[6])
{
    return state[0] >= 0 && state[1] >= 0 && state[2] >= 0 && state[2] <= 3 &&
           state[3] >= 0 && state[3] <= 15 && !state[4] && !state[5];
}
static LONG spawn(char role)
{
    char tail[3] = { 1, 0, 0 };
    static const char environment[] = { 0, 0 };
    tail[1] = role;
    return process(0x4b, 100, (uintptr_t)"C:\\GEMEVPE.TOS", (uintptr_t)tail, (uintptr_t)environment);
}
int main(int argc, char **argv, char **envp)
{
    short id, parent, message[8], received[8], global[15], state[6];
    UBYTE payload[80], excess[64];
    ULONG i;
    LONG pid, status;
    (void)envp;
    for (i = 0; i < 6; i++) state[i] = 99;
    VERIFY(multi(MU_TIMER, 0, NULL, state, NULL) == 0 && state[0] == 99);
    VERIFY(graf_mkstate(&state[0], &state[1], &state[2], &state[3]) == 0 && state[0] == 99);
    id = appl_init(); VERIFY(id > 0 && mt_appl_init(global) == id);
    VERIFY(graf_mkstate(&state[0], &state[1], &state[2], &state[3]) == 1);
    if (argc > 1) {
        if (argv[1][0] == 'f') {
            (void)evnt_multi(MU_TIMER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, 0,
                             (short *)(uintptr_t)main, NULL, NULL, NULL, NULL, NULL);
            return 99; /* A bad scalar output is an ordinary user fault. */
        }
        parent = appl_find("GEMEVNT "); VERIFY(parent > 0 && parent != id);
        for (i = 0; i < 8; i++) message[i] = 0;
        message[1] = id;
        if (argv[1][0] != 'z') {
            message[0] = 900; VERIFY(appl_write(parent, 16, message) == 1);
            VERIFY(evnt_timer(argv[1][0] == 't' ? 500 : 20) == 1);
        }
        message[0] = 901; VERIFY(appl_write(parent, 16, message) == 1);
        return 37;
    }
    for (i = 0; i < 8; i++) message[i] = (short)(0x722 + i);
    message[1] = id; message[2] = 0;
    VERIFY(multi(MU_TIMER, 0, NULL, state, NULL) == MU_TIMER && state_valid(state));
    VERIFY(appl_write(id, 16, message) == 1);
    VERIFY(multi(MU_TIMER, 1, (short *)(uintptr_t)1, state, global) == MU_TIMER && state_valid(state));
    VERIFY(appl_read(-1, 16, received) == 1);
    VERIFY(appl_write(id, 16, message) == 1);
    VERIFY(multi(MU_MESAG | MU_TIMER, 0, received, state, NULL) == (MU_MESAG | MU_TIMER) && state_valid(state));
    for (i = 0; i < 8; i++) VERIFY(received[i] == message[i]);
    VERIFY(appl_read(-1, 16, received) == 0);
    message[2] = 64;
    for (i = 0; i < 16; i++) payload[i] = ((UBYTE *)message)[i];
    for (i = 16; i < 80; i++) payload[i] = (UBYTE)i;
    VERIFY(appl_write(id, 80, payload) == 1);
    VERIFY(multi(MU_MESAG | MU_TIMER, 1000, received, state, global) == MU_MESAG && received[2] == 64);
    VERIFY(appl_read(id, 64, excess) == 1);
    for (i = 0; i < 64; i++) VERIFY(excess[i] == payload[i + 16]);
    message[2] = 0;
    VERIFY(appl_write(id, 16, message) == 1);
    for (i = 0; i < 6; i++) state[i] = 99;
    received[0] = 123;
    VERIFY(multi(MU_KEYBD | MU_MESAG, 0, received, state, NULL) == 0 && received[0] == 123 && state[0] == 99);
    VERIFY(multi(0, 0, received, state, NULL) == 0 && appl_read(-1, 16, received) == 1);
    VERIFY(appl_write(id, 15, message) == 1);
    received[0] = 123;
    VERIFY(multi(MU_MESAG | MU_TIMER, 1, received, state, NULL) == MU_TIMER && received[0] == 123);
    VERIFY(appl_read(-1, 16, received) == 0);
    VERIFY(appl_write(id, 1, (UBYTE *)message + 15) == 1);
    VERIFY(multi(MU_MESAG | MU_TIMER, 0, received, state, NULL) == (MU_MESAG | MU_TIMER));
    VERIFY(appl_write(id, 16, message) == 1);
    for (i = 0; i < 6; i++) state[i] = 99;
    VERIFY(multi(MU_MESAG, ~0UL, (short *)(uintptr_t)main, state, NULL) == 0 && state[0] == 99);
    VERIFY(appl_read(-1, 16, received) == 1); /* Failed copyout retained the message. */
    pid = spawn('p'); VERIFY(pid > 0);
    VERIFY(evnt_mesag(received) == 1 && received[0] == 900);
    VERIFY(multi(MU_MESAG | MU_TIMER, 1000, received, state, NULL) == MU_MESAG && received[0] == 901 && received[1] == pid);
    VERIFY(state_valid(state) && process(0x13a, pid, 0, 0, 0) == (LONG)(((ULONG)pid << 16) | 37));
    pid = spawn('t'); VERIFY(pid > 0);
    VERIFY(evnt_mesag(received) == 1 && received[0] == 900);
    received[0] = 123;
    VERIFY(multi(MU_MESAG | MU_TIMER, 1, received, state, global) == MU_TIMER && received[0] == 123);
    VERIFY(multi(MU_MESAG, ~0UL, received, state, global) == MU_MESAG && received[0] == 901 && received[1] == pid);
    VERIFY(process(0x13a, pid, 0, 0, 0) == (LONG)(((ULONG)pid << 16) | 37));
    pid = spawn('z'); VERIFY(pid > 0);
    VERIFY(multi(MU_MESAG | MU_TIMER, 0, received, state, NULL) == (MU_MESAG | MU_TIMER) && received[1] == pid);
    VERIFY(process(0x13a, pid, 0, 0, 0) == (LONG)(((ULONG)pid << 16) | 37));
    pid = spawn('f'); VERIFY(pid > 0);
    status = process(0x13a, pid, 0, 0, 0);
    VERIFY(status == (LONG)(((ULONG)pid << 16) | 0x0b00));
    VERIFY(appl_find("GEMEVPE ") == -1 && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM combined events: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
    }
    return 0;
}
