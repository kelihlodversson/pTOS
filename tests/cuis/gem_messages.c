/* Normal GEM source interfaces across isolated, dynamically linked clients. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#define VERIFY(c) do { if (!(c)) return 0x7100 | __LINE__; } while (0)
static UBYTE data[32767], received[32767];
static LONG process(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static BOOL equal(const UBYTE *a, const UBYTE *b, ULONG length)
{
    ULONG i;
    for (i = 0; i < length; i++) if (a[i] != b[i]) return FALSE;
    return TRUE;
}
static LONG spawn(char role)
{
    char tail[3] = { 1, 0, 0 };
    static const char environment[] = { 0, 0 };
    tail[1] = role;
    return process(0x4b, 100, (uintptr_t)"C:\\GEMPEER.TOS", (uintptr_t)tail, (uintptr_t)environment);
}
int main(int argc, char **argv, char **envp)
{
    short id, parent, peer, message[8], header[8], other[15];
    short a, b, c, d;
    LONG pid, status;
    ULONG i;
    (void)envp;
    id = appl_init(); VERIFY(id > 0);
    VERIFY(appl_find(NULL) == id && mt_appl_init(other) == id);
    VERIFY(appl_getinfo(4, &a, &b, &c, &d) == 1 && !a && b == 1 && !c && d == 1);
    VERIFY(appl_getinfo(6, &a, &b, &c, &d) == 1 && a == 1 && !b && !c && !d);
    for (i = 0; i < sizeof(data); i++) data[i] = (UBYTE)(i * 7 + 3);
    if (argc > 1) {
        parent = appl_find("gemmsg"); VERIFY(parent > 0 && parent != id);
        VERIFY(appl_find("GEMPEER ") == id);
        message[0] = 700; message[1] = id;
        for (i = 2; i < 8; i++) message[i] = 0;
        VERIFY(appl_write(parent, 16, message) == 1);
        if (argv[1][0] == 'd') {
            VERIFY(evnt_mesag(header) == 1 && header[0] == 800);
            VERIFY(appl_exit() == 1 && appl_init() == id);
            VERIFY(appl_read(-1, 16, header) == 0); /* Reopen did not inherit the old pipe. */
            message[0] = 801;
            VERIFY(appl_write(parent, 16, message) == 1 && evnt_timer(1) == 1);
            return 41;
        }
        VERIFY(mt_evnt_mesag(header, other) == 1 && header[0] == 701 && header[1] == parent && header[2] == 64);
        VERIFY(appl_read(id, 64, received) == 1 && equal(received, data + 16, 64));
        message[0] = 702; VERIFY(mt_appl_write(parent, 16, message, other) == 1);
        VERIFY(appl_read(id, 32767, received) == 1 && equal(received, data, sizeof(data)));
        VERIFY(appl_yield() == 1); /* Resume the blocked writer after freeing ring space. */
        VERIFY(appl_read(id, 32767, received) == 1 && equal(received, data, sizeof(data)));
        VERIFY(appl_read(id, 2, received) == 1 && equal(received, data, 2));
        VERIFY(evnt_mesag(header) == 1 && header[0] == 703);
        message[0] = 704; VERIFY(appl_write(parent, 16, message) == 1);
        VERIFY(appl_exit() == 1);
        VERIFY(process(0xff, 0, 0, 0, 0) == 0); /* Let the parent observe discovery removal. */
        VERIFY(appl_init() == id);
        message[0] = 705; VERIFY(appl_write(parent, 16, message) == 1);
        return 37; /* The queued reply survives the sender's finalization/exit. */
    }
    VERIFY(appl_find("GEMMSG  ") == id);
    VERIFY(appl_find((const char *)(uintptr_t)(0xffff0000UL | (UWORD)id)) == id);
    VERIFY(appl_find((const char *)(uintptr_t)(0xfffe0000UL | (UWORD)id)) == id);
    VERIFY(appl_find("ABSENT  ") == -1);
    VERIFY(appl_read(-1, 16, header) == 0 && appl_read(id, -1, header) == 0);
    VERIFY(appl_write(id, 0, NULL) == 1 && appl_read(id, 0, NULL) == 1);
    VERIFY(appl_write(id, 32767, data) == 1);
    VERIFY(mt_appl_read(-1, 32767, received, other) == 1 && equal(data, received, sizeof(data)));
    /* Reads cross writes, and user payload words are not rewritten. */
    VERIFY(appl_write(id, 7, data) == 1 && appl_write(id, 9, data + 7) == 1);
    VERIFY(evnt_mesag(header) == 1 && equal((UBYTE *)header, data, 16));
    pid = spawn('p'); VERIFY(pid > 0);
    VERIFY(evnt_mesag(message) == 1 && message[0] == 700 && message[1] == pid);
    peer = appl_find("GEMPEER "); VERIFY(peer == pid);
    header[0] = 701; header[1] = id; header[2] = 64;
    for (i = 3; i < 8; i++) header[i] = 0;
    for (i = 0; i < 16; i++) data[i] = ((UBYTE *)header)[i];
    VERIFY(appl_write(peer, 80, data) == 1);
    VERIFY(evnt_mesag(message) == 1 && message[0] == 702);
    for (i = 0; i < sizeof(data); i++) data[i] = (UBYTE)(i * 7 + 3);
    VERIFY(appl_write(peer, 32767, data) == 1 && appl_write(peer, 32767, data) == 1);
    VERIFY(appl_write(peer, 2, data) == 1);
    message[0] = 703; message[1] = id;
    VERIFY(appl_write(peer, 16, message) == 1);
    VERIFY(evnt_mesag(message) == 1 && message[0] == 704);
    VERIFY(appl_find("GEMPEER ") == -1);
    VERIFY(appl_find((const char *)(uintptr_t)(0xffff0000UL | (UWORD)peer)) == -1);
    VERIFY(appl_write(peer, 16, message) == 0);
    VERIFY(evnt_mesag(message) == 1 && message[0] == 705 && message[1] == peer);
    status = process(0x13a, pid, 0, 0, 0);
    VERIFY(status == (LONG)(((ULONG)pid << 16) | 37));
    VERIFY(appl_find("GEMPEER ") == -1);
    pid = spawn('d'); VERIFY(pid > 0);
    VERIFY(evnt_mesag(message) == 1 && message[0] == 700 && message[1] == pid);
    peer = (short)pid;
    message[0] = 800; message[1] = id;
    for (i = 0; i < 16; i++) data[i] = ((UBYTE *)message)[i];
    VERIFY(appl_write(peer, 32767, data) == 1 && appl_write(peer, 32767, data) == 1);
    VERIFY(appl_write(peer, 2, data) == 1);
    VERIFY(appl_write(peer, 16, message) == 0); /* Receiver closed/reopened while this write slept. */
    VERIFY(evnt_mesag(message) == 1 && message[0] == 801);
    VERIFY(process(0x13a, pid, 0, 0, 0) == (LONG)(((ULONG)pid << 16) | 41));
    VERIFY(appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM discovery/messages: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
    }
    return 0;
}
