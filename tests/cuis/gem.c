/* Standard GEM source calls, with raw process/queue probes kept test-only. */
#include <gem.h>
#include "cuis.h"
#include "gemerror.h"
#include "ptos.h"
#define GEM_TEST_CHECK(c) do { if (!(c)) return 0x7000 | __LINE__; } while (0)
static LONG raw(ULONG cls, ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)cls << 32) | fn;
    register UQUAD fourth __asm__("r10") = d, fifth __asm__("r8") = e, sixth __asm__("r9") = f;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth), "r"(fifth), "r"(sixth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
#define PROC(fn,a,b,c,d) raw(1,fn,a,b,c,d,0,0)
int main(int argc, char **argv, char **envp)
{
    short id, globals[15], a = 1, b = 2, c = 3, d = 4;
    LONG pid, handle, child, status, sender;
    ULONG w[4], i;
    char tail[3] = { 1, 'c', 0 };
    static const char environment[] = { 0, 0 };
    (void)envp;
    GEM_TEST_CHECK(argc >= 1 && gl_apid == -1 && aes_global[2] == -1 && aes_global[3] == 0);
    GEM_TEST_CHECK(appl_yield() == 0 && evnt_timer(0) == 0 && appl_exit() == 0);
    pid = PROC(0x10b, 0, 0, 0, 0);
    id = appl_init();
    GEM_TEST_CHECK(id > 0 && id == pid && gl_apid == id && aes_global[2] == id && aes_global[1] == -1);
    GEM_TEST_CHECK(appl_init() == id && mt_appl_init(globals) == id);
    aes_global[3] = 1234;
    GEM_TEST_CHECK(mt_appl_yield(globals) == 1 && mt_evnt_timer(1, globals) == 1);
    GEM_TEST_CHECK(appl_getinfo(4, &a, &b, &c, &d) == 0 && !(a | b | c | d));
    if (argc > 1) {
        if (argv[1][0] == 'x') _Exit(41);
        return 37; /* Ordinary CRT finalization closes implicit registration. */
    }
    handle = raw(CUIS_SERVICE_CLASS, CUIS_SESSION_START, 0, 0, 0, 0, 0, 0);
    GEM_TEST_CHECK(handle >= 0);
    for (i = 0; i < 2; i++) {
        GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS, CUIS_MESSAGE_SEND, handle, 11, 22, 33, 44, 0) == E_OK);
        tail[1] = i ? 'x' : 'c';
        child = PROC(0x4b, 100, (uintptr_t)argv[0], (uintptr_t)tail, (uintptr_t)environment);
        GEM_TEST_CHECK(child > 0 && child != id);
        GEM_TEST_CHECK(evnt_timer(20) == 1 && appl_yield() == 1);
        status = PROC(0x13a, child, 0, 0, 0);
        GEM_TEST_CHECK(status == (LONG)(((ULONG)child << 16) | (i ? 41 : 37)));
        GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS, CUIS_MESSAGE_READ, handle, (uintptr_t)&sender,
                  (uintptr_t)&w[0], (uintptr_t)&w[1], (uintptr_t)&w[2], (uintptr_t)&w[3]) == 1);
        GEM_TEST_CHECK(sender == handle && w[0] == 11 && w[1] == 22 && w[2] == 33 && w[3] == 44);
        GEM_TEST_CHECK(aes_global[3] == 1234 && gl_apid == id && globals[2] == id);
    }
    GEM_TEST_CHECK(evnt_timer(0) == 1 && mt_appl_exit(globals) == 1);
    GEM_TEST_CHECK(gl_apid == -1 && aes_global[2] == -1 && globals[2] == -1);
    GEM_TEST_CHECK(appl_exit() == 0 && appl_init() == id && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM lifecycle/timers: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
    }
    return 0;
}
