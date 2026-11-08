/* Isolated native clients exercise scheduling through active byte streams. */
#include "stream_probe.h"
#include "gemerror.h"
#include "ptos.h"
extern int64_t client_pexec(const char *, const char *, const char *);
#undef CHECK
#define CHECK(c) do { if (!(c)) return 0x5000 | __LINE__; } while (0)
static volatile ULONG private_state;
static void yield_peer(void)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | 0xff;
    __asm__ volatile ("syscall" : "+a"(ax) : : "rcx","r11","memory","cc");
}
static LONG wait_cookie(LONG session, UQUAD cookie)
{
    UQUAD actual;
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | CUIS_SESSION_WAIT;
    register UQUAD di __asm__("rdi") = (QUAD)session, si __asm__("rsi") = sizeof(PROBE_PACKET);
    register UQUAD dx __asm__("rdx") = CUIS_READY_STREAM, fourth __asm__("r10") = 0;
    __asm__ volatile ("movq %[cookie], %%xmm7; syscall; movq %%xmm7, %[actual]"
        : "+a"(ax), [actual] "=r"(actual)
        : "r"(di), "r"(si), "r"(dx), "r"(fourth), [cookie] "r"(cookie)
        : "rcx", "r11", "xmm7", "memory", "cc");
    return actual == cookie ? (LONG)ax : EIMBA;
}
int main(int argc, char **argv, char **envp)
{
    LONG session, peer, window;
    PROBE_PACKET packet;
    ULONG i, expected;
    BOOL first;
    static const char label_a[] = "SCHED-A ", label_b[] = "SCHED-B ";
    static const char nested_tail[] = { 1, 'n', 0 }, environment[] = { 0, 0 };
    (void)envp;
    CHECK(argc == 2);
    session = probe_session_open(); CHECK(session >= 0);
    if (argv[1][0] == 'n') {
        CHECK(probe_packet_wait(session,5) == CUIS_READY_TIMER); return 0;
    }
    first = argv[1][0] == 'a';
    CHECK(probe_call6(CUIS_SESSION_LABEL,session,(uintptr_t)(first ? label_a : label_b),8,0,0,0) == E_OK);
    do {
        peer = probe_call6(CUIS_LABEL_FIND,(uintptr_t)(first ? label_b : label_a),8,0,0,0,0);
        if (peer == EFILNF) yield_peer();
    } while (peer == EFILNF);
    CHECK(peer >= 0 && peer != session);
    private_state = expected = first ? 0x12345678UL : 0x87654321UL;
    for (i = 0; i < 64; i++) {
        if (!first) CHECK(probe_packet_write(peer,session,i,2,3,4) == sizeof(packet));
        CHECK(wait_cookie(session,((UQUAD)expected << 32) | i) == CUIS_READY_STREAM);
        CHECK(private_state == expected && probe_session_open() == session);
        CHECK(probe_packet_read(session,&packet) == sizeof(packet));
        CHECK(packet.endpoint == peer && packet.word[0] == i && packet.word[1] == 2 && packet.word[2] == 3 && packet.word[3] == 4);
        if (first) CHECK(probe_packet_write(peer,session,i,2,3,4) == sizeof(packet));
    }
    window = probe_window_create(0,0,10,10); CHECK(window >= 0);
    CHECK(probe_call6(CUIS_WINDOW_POLICY,window,1,0,0,0,0) == E_OK);
    if (first) {
        CHECK(probe_window_show(window) == E_OK);
        CHECK(probe_packet_write(peer,session,65,0,0,0) == sizeof(packet));
        CHECK(probe_event_wait(window,CUIS_EVENT_KEY,-1) == EACCDN);
        CHECK(probe_packet_write(peer,session,66,0,0,0) == sizeof(packet));
    } else {
        CHECK(probe_packet_wait(session,-1) == CUIS_READY_STREAM);
        CHECK(probe_packet_read(session,&packet) == sizeof(packet) && packet.word[0] == 65);
        CHECK(probe_window_show(window) == E_OK);
        CHECK(probe_packet_wait(session,-1) == CUIS_READY_STREAM);
        CHECK(probe_packet_read(session,&packet) == sizeof(packet) && packet.word[0] == 66);
    }
    CHECK(probe_window_destroy(window) == E_OK);
    if (first) CHECK(client_pexec(argv[0],nested_tail,environment) == 0);
    CHECK(probe_packet_wait(session,first ? 10 : 15) == CUIS_READY_TIMER);
    CHECK(private_state == expected);
    if (!first && argv[1][1] == '1') __asm__ volatile ("ud2");
    if (!first && argv[1][1] == '2') _Exit(77);
    return 0;
}
