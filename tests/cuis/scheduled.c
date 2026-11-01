/* Real isolated clients exercise the kernel scheduler, not an AES coroutine. */
#include "libaes.h"
#include "gemerror.h"
#include "ptos.h"
extern int64_t client_pexec(const char *, const char *, const char *);
#define CHECK(c) do { if (!(c)) return 0x5000 | __LINE__; } while (0)
static volatile ULONG private_state;

static LONG wait_cookie(LONG session, UQUAD cookie)
{
    UQUAD actual;
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | CUIS_MESSAGE_WAIT;
    register UQUAD di __asm__("rdi") = (QUAD)session;
    register UQUAD si __asm__("rsi") = -1ULL;
    __asm__ volatile ("movq %[cookie], %%xmm7; syscall; movq %%xmm7, %[actual]"
        : "+a"(ax), [actual] "=r"(actual)
        : "r"(di), "r"(si), [cookie] "r"(cookie)
        : "rcx", "r11", "xmm7", "memory", "cc");
    return actual == cookie ? (LONG)ax : EIMBA;
}
int main(int argc, char **argv, char **envp)
{
    LONG session, sender, peer, window;
    ULONG a, b, c, d, i, expected;
    BOOL first;
    static const char nested_tail[] = { 1, 'n', 0 };
    static const char environment[] = { 0, 0 };
    (void)envp;
    CHECK(argc == 2);
    session = aes_session_open();
    CHECK(session >= 0);
    if (argv[1][0] == 'n') {
        CHECK(aes_message_wait(session, 5) == 0);
        return 0;
    }
    first = argv[1][0] == 'a';
    private_state = expected = first ? 0x12345678UL : 0x87654321UL;
    CHECK(aes_message_read(session, &sender, &a, &b, &c, &d) == 1 && sender == session);
    peer = (LONG)a;
    for (i = 0; i < 64; i++) {
        if (!first) CHECK(aes_message_send(peer, i, 2, 3, 4) == E_OK);
        CHECK(wait_cookie(session, ((UQUAD)expected << 32) | i) == 1);
        CHECK(private_state == expected && aes_session_open() == session);
        CHECK(aes_message_read(session, &sender, &a, &b, &c, &d) == 1);
        CHECK(sender == peer && a == i && b == 2 && c == 3 && d == 4);
        if (first) CHECK(aes_message_send(peer, i, 2, 3, 4) == E_OK);
    }
    window = aes_window_create(0, 0, 10, 10);
    CHECK(window >= 0);
    if (first) {
        CHECK(aes_window_show(window) == E_OK && aes_window_focus(window) == E_OK);
        CHECK(aes_message_send(peer, 65, 0, 0, 0) == E_OK);
        CHECK(aes_key_wait(window, -1) == EACCDN); /* Another task takes focus. */
        CHECK(aes_message_send(peer, 66, 0, 0, 0) == E_OK);
    } else {
        CHECK(aes_message_wait(session, -1) == 1);
        CHECK(aes_message_read(session, &sender, &a, &b, &c, &d) == 1 && a == 65);
        CHECK(aes_window_show(window) == E_OK && aes_window_focus(window) == E_OK);
        CHECK(aes_message_wait(session, -1) == 1);
        CHECK(aes_message_read(session, &sender, &a, &b, &c, &d) == 1 && a == 66);
    }
    CHECK(aes_window_destroy(window) == E_OK);
    if (first) CHECK(client_pexec(argv[0], nested_tail, environment) == 0);
    CHECK(aes_message_wait(session, first ? 10 : 15) == 0);
    CHECK(private_state == expected);
    if (!first && argv[1][1] == '1') __asm__ volatile ("ud2");
    if (!first && argv[1][1] == '2') _Exit(77);
    return 0;
}
