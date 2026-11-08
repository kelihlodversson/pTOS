/* Real standard-ELF clients, with overlapping lifetimes in distinct spaces. */
#include "config.h"
#include "stream_probe.h"
#include "gemerror.h"
#include "ptos.h"
int libaes_object_checks(void);
int libaes_drawing_checks(void);
int libaes_drawing_exit_handle(void);
typedef char object_x32_size[(sizeof(OBJECT) == 24) ? 1 : -1];
extern int64_t client_pexec(const char *path, const char *tail, const char *env);

/* Deliberately poison unused slots. Session calls must never inspect them. */
static LONG raw(ULONG function, UQUAD handle)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD di __asm__("rdi") = handle;
    register UQUAD si __asm__("rsi") = ~0ULL, dx __asm__("rdx") = ~0ULL;
    register UQUAD r10 __asm__("r10") = ~0ULL, r8 __asm__("r8") = ~0ULL;
    register UQUAD r9 __asm__("r9") = ~0ULL;
    __asm__ volatile ("syscall" : "+a"(ax)
        : "r"(di), "r"(si), "r"(dx), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
#undef CHECK
#define CHECK(condition) do { if (!(condition)) return 0x4000 | __LINE__; } while (0)
/* Poison high address bits without letting the x32 C pointer ABI truncate. */
static LONG raw_clip(LONG window, UQUAD index, LONG *x, LONG *y, LONG *w, UQUAD height)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | CUIS_WINDOW_CLIP;
    register UQUAD di __asm__("rdi") = (QUAD)window, si __asm__("rsi") = index;
    register UQUAD dx __asm__("rdx") = (uintptr_t)x, r10 __asm__("r10") = (uintptr_t)y;
    register UQUAD r8 __asm__("r8") = (uintptr_t)w, r9 __asm__("r9") = height;
    __asm__ volatile ("syscall" : "+a"(ax)
        : "r"(di), "r"(si), "r"(dx), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static int update_checks(LONG window)
{
    LONG x, y, width, height, occluder;
    CHECK(probe_update_begin(window) == E_OK);
    CHECK(probe_window_clip(window, 0, &x, &y, &width, &height) == 1);
    CHECK(x == 0 && y == 30 && width == 100 && height == 90);
    x = 777;
    CHECK(probe_window_clip(window, 0, &x, &y, &width, (LONG *)0) == EIMBA);
    CHECK(x == 777);
    CHECK(raw_clip(window, 0, &x, &y, &width, (1ULL << 32) | (uintptr_t)&height) == EIMBA);
    CHECK(x == 777);
    CHECK(probe_window_clip(window, 0, &x, &y, &width, (LONG *)(uintptr_t)raw) == EIMBA);
    CHECK(raw_clip(window, 1ULL << 32, &x, &y, &width, (uintptr_t)&height) == EINVAL);
    CHECK(probe_window_clip(window, 0, &x, &y, &width, &height) == 1);
    CHECK(probe_window_clip(window, 1, (LONG *)0, (LONG *)0, (LONG *)0, (LONG *)0) == 0);
    CHECK(probe_update_begin(window) == EAGAIN);
    CHECK(probe_window_hide(window) == EAGAIN);
    CHECK(probe_update_end(window) == E_OK);
    occluder = probe_window_create(0, 40, 20, 10);
    CHECK(occluder >= 0 && probe_window_show(occluder) == E_OK);
    CHECK(probe_update_begin(window) == E_OK);
    CHECK(probe_window_clip(window, 0, &x, &y, &width, &height) == 1);
    CHECK(x == 0 && y == 30 && width == 100 && height == 10);
    CHECK(probe_window_clip(window, 1, &x, &y, &width, &height) == 1);
    CHECK(x == 0 && y == 50 && width == 100 && height == 70);
    CHECK(probe_window_clip(window, 2, &x, &y, &width, &height) == 1);
    CHECK(x == 20 && y == 40 && width == 80 && height == 10);
    CHECK(probe_window_clip(window, 3, &x, &y, &width, &height) == 0);
    CHECK(probe_update_end(window) == E_OK);
    CHECK(probe_window_destroy(occluder) == E_OK);
    return 0;
}
static int key_checks(LONG window)
{
    CHECK(probe_call6(CUIS_WINDOW_POLICY,window,1,0,0,0,0) == E_OK);
    CHECK(probe_window_raise(window) == E_OK);
    CHECK(probe_event_wait(window,CUIS_EVENT_KEY,0) == 0);
    CHECK(probe_event_wait(window,CUIS_EVENT_KEY,-2) == EINVAL);
    CHECK(probe_event_wait(window,CUIS_EVENT_KEY,1) == 0);
#if CONF_WITH_X86_64_MEMTEST
    {
        LONG session=probe_session_open();
        LONG values[5];
        CHECK(probe_event_wait(window,CUIS_EVENT_KEY,-1) == 1);
        CHECK(probe_call6(CUIS_SESSION_WAIT,session,0,CUIS_READY_KEY,0,0,0) == CUIS_READY_KEY);
        CHECK(probe_call6(CUIS_INPUT_RESULT,session,0,(uintptr_t)&values[1],(uintptr_t)&values[2],
                         (uintptr_t)&values[3],(uintptr_t)&values[4]) == EIMBA);
        CHECK(probe_call6(CUIS_INPUT_RESULT,session,(1ULL<<32)|(uintptr_t)&values[0],(uintptr_t)&values[1],
                         (uintptr_t)&values[2],(uintptr_t)&values[3],(uintptr_t)&values[4]) == EIMBA);
        CHECK(probe_call6(CUIS_INPUT_RESULT,session,(uintptr_t)&values[0],(uintptr_t)&values[1],
                         (uintptr_t)&values[2],(uintptr_t)&values[3],(uintptr_t)&values[4]) == E_OK);
        CHECK(((values[0] >> 16)&255) == 0x25);
        CHECK(probe_event_wait(window,CUIS_EVENT_KEY,0) == 0);
    }
#endif
    return 0;
}
#if CONF_WITH_X86_64_MEMTEST
static LONG raw_pointer_read(LONG window, LONG *x, LONG *y, ULONG *buttons, UQUAD state)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | CUIS_POINTER_READ;
    register UQUAD di __asm__("rdi") = (QUAD)window, si __asm__("rsi") = (uintptr_t)x;
    register UQUAD dx __asm__("rdx") = (uintptr_t)y, r10 __asm__("r10") = (uintptr_t)buttons;
    register UQUAD r8 __asm__("r8") = state;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di), "r"(si), "r"(dx), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
#endif
static int pointer_checks(void)
{
    LONG window, x, y, rc;
    ULONG buttons, state, i;
    window = probe_window_create(0, 0, 0x7fffffffL, 0x7fffffffL);
    CHECK(window >= 0 && probe_window_show(window) == E_OK);
    for (i = 0; i < 17; i++) {
        rc = probe_pointer_read(window, &x, &y, &buttons, &state);
        CHECK(rc >= 0);
        if (!rc) break;
    }
    CHECK(rc == 0);
    CHECK(probe_event_wait(window, CUIS_EVENT_REDRAW | CUIS_EVENT_POINTER, 0) == 1);
    CHECK(probe_event_wait(window, CUIS_EVENT_KEY, 0) == EACCDN);
    CHECK(probe_event_wait(window, CUIS_EVENT_POINTER | CUIS_EVENT_KEY, 0) == 0);
    CHECK(probe_event_wait(window, 0, 0) == EINVAL);
    CHECK(probe_event_wait(window, 32, 0) == EINVAL);
    CHECK(probe_event_wait(window, CUIS_EVENT_POINTER, -2) == EINVAL);
    CHECK(probe_event_wait(window, CUIS_EVENT_POINTER, 1) == 0);
#if CONF_WITH_X86_64_MEMTEST
    /* A real IRQ delivers a press and release before this waiter resumes. */
    CHECK(probe_event_wait(window, CUIS_EVENT_KEY | CUIS_EVENT_POINTER, -1) == 1);
    CHECK(probe_window_focused(window) == 1);
    CHECK(probe_pointer_read(window, &x, &y, &buttons, (ULONG *)0) == EIMBA);
    CHECK(raw_pointer_read(window, &x, &y, &buttons, (1ULL << 32) | (uintptr_t)&state) == EIMBA);
    CHECK(probe_pointer_read(window, &x, &y, &buttons, &state) == 1);
    CHECK(buttons == 1 && (state & CUIS_POINTER_CHANGED) == 1 && (state & CUIS_POINTER_INSIDE));
    CHECK(probe_pointer_read(window, &x, &y, &buttons, &state) == 1);
    CHECK(buttons == 0 && (state & CUIS_POINTER_CHANGED) == 1);
    CHECK(probe_pointer_read(window, &x, &y, &buttons, &state) == 0);
#endif
    CHECK(probe_window_destroy(window) == E_OK);
    return 0;
}
static int message_checks(LONG session, LONG window)
{
    PROBE_PACKET packet;
    LONG ready;
    (void)window;
    CHECK(probe_packet_wait(session,0) == CUIS_READY_TIMER);
    CHECK(probe_packet_write(session,session,7,22,33,44) == sizeof(packet));
    CHECK(probe_packet_wait(session,-1) == CUIS_READY_STREAM);
    /* Readiness does not consume; rejected output cannot consume bytes. */
    CHECK(probe_call6(CUIS_STREAM_READ,session,0,sizeof(packet),0,0,0) == EIMBA);
    CHECK(probe_packet_wait(session,-1) == CUIS_READY_STREAM);
    CHECK(probe_packet_read(session,&packet) == sizeof(packet));
    CHECK(packet.endpoint == session && packet.word[0] == 7 && packet.word[3] == 44);
    ready = probe_packet_wait(session,1);
    CHECK(ready == CUIS_READY_TIMER);
    return 0;
}
int main(int argc, char **argv, char **envp)
{
    GRECT first = { 0, 0, 20, 20 }, second = { 10, 10, 20, 20 };
    LONG handle, window;
    ULONG owner = 0, parent_window = 0, function;
    char tail[] = { 17, 'c', '0', '0', '0', '0', '0', '0', '0', '0',
                    '0', '0', '0', '0', '0', '0', '0', '0', 0 };
    static const char digits[] = "0123456789abcdef";
    int i, rc;
    (void)envp;
    rc = libaes_object_checks();
    if (rc) return rc;
    CHECK(argc == 1 || argc == 2);
    CHECK(raw(99, ~0ULL) == EINVFN);
    CHECK(probe_session_query(0) == EACCDN);
    if (argc == 2) {
        for (i = 1; i <= 8; i++) {
            char c = argv[1][i];
            CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            owner = (owner << 4) | (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        for (i = 9; i <= 16; i++) {
            char c = argv[1][i];
            CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            parent_window = (parent_window << 4) | (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        CHECK(probe_session_query(owner) == EACCDN);
        CHECK(probe_session_close(owner) == EACCDN);
        CHECK(probe_call6(CUIS_STREAM_READ,owner,0,20,0,0,0) == EACCDN);
        CHECK(probe_packet_write(owner, 0, 1, 2, 3, 4) == EACCDN);
        CHECK(probe_call6(CUIS_INPUT_RESULT,owner,0,0,0,0,0) == EACCDN);
        CHECK(probe_event_wait(parent_window, CUIS_EVENT_KEY, 0) == EACCDN);
        CHECK(probe_event_wait(parent_window, CUIS_EVENT_POINTER, 0) == EACCDN);
        CHECK(probe_pointer_read(parent_window, (LONG *)0, (LONG *)0, (ULONG *)0, (ULONG *)0) == EACCDN);
    }
    handle = probe_session_open();
    CHECK(handle >= 0 && probe_session_open() == handle);
    if (argc == 2) {
        /* GEM appl_write needs no receiver handshake or permission API. */
        CHECK(probe_packet_write(owner, handle, 1, 2, 3, 4) == sizeof(PROBE_PACKET));
    }
    CHECK(raw(CUIS_SESSION_START, ~0ULL) == handle);
    CHECK(raw(CUIS_SESSION_QUERY, (1ULL << 32) | (ULONG)handle) == EINVAL);
    CHECK(raw(CUIS_SESSION_END, (1ULL << 32) | (ULONG)handle) == EINVAL);
    CHECK(raw(CUIS_SESSION_END, ~0ULL) == EINVAL);
    CHECK(probe_session_query(-1) == EINVAL);
    CHECK(raw(CUIS_SESSION_QUERY, handle) == E_OK);
    if (argc == 2 && argv[1][0] == 'u') {
        LONG x, y, w, h;
        rc = libaes_drawing_exit_handle();
        if (rc) return rc;
        CHECK(probe_window_create(0, 0, 1, 1) == EAGAIN);
        CHECK(probe_update_end(parent_window) == EACCDN);
        CHECK(probe_window_focused(parent_window) == EACCDN);
        CHECK(probe_window_clip(parent_window, 0, &x, &y, &w, &h) == EACCDN);
        return 77;
    }
    window = probe_window_create(-10, 20, 100, 80);
    CHECK(window >= 0);
    CHECK(probe_window_raise(window) == EINVAL);
    CHECK(probe_window_show(window) == E_OK);
    CHECK(probe_window_hide(window) == E_OK);
    CHECK(probe_window_show(window) == E_OK);
    CHECK(probe_window_lower(window) == E_OK);
    CHECK(probe_window_raise(window) == E_OK);
    CHECK(probe_window_configure(window, -20, 30, 120, 90) == E_OK);
    CHECK(probe_window_configure(window, -20, 30, 120, 90) == E_OK);
    CHECK(probe_window_configure(window, 1, 2, 3, 0) == EINVAL);
    CHECK(probe_window_destroy(-1) == EINVAL);
    CHECK(probe_window_create(0x7fffffffL, 0, 1, 1) == EINVAL);
    CHECK(rc_intersect(&first, &second) && second.g_x == 10 && second.g_y == 10 &&
          second.g_w == 10 && second.g_h == 10 && inside(19, 19, &second));
    rc = update_checks(window);
    if (rc) return rc;
    CHECK(probe_call6(CUIS_WINDOW_POLICY,window,1,0,0,0,0) == E_OK);
    CHECK(probe_window_raise(window) == E_OK);
    CHECK(probe_window_focused(window) == 1);
    CHECK(probe_window_hide(window) == E_OK && probe_window_focused(window) == 0);
    CHECK(probe_window_show(window) == E_OK);
    CHECK(probe_call6(CUIS_WINDOW_POLICY,window,1,0,0,0,0) == E_OK);
    CHECK(probe_window_raise(window) == E_OK);
    if (argc == 2) {
        CHECK(probe_update_begin(window) == E_OK);
        CHECK(probe_window_focused(window) == 1);
        CHECK(probe_window_hide(window) == EAGAIN);
        CHECK(probe_window_raise(parent_window) == EACCDN);
        rc = libaes_drawing_exit_handle();
        if (rc) return rc;
        for (function = CUIS_WINDOW_SHOW; function <= CUIS_WINDOW_LOWER; function++)
            CHECK(raw(function, parent_window) == EACCDN);
        CHECK(probe_window_destroy(parent_window) == EACCDN);
        CHECK(probe_window_configure(parent_window, 0, 0, 1, 1) == EACCDN);
        CHECK((ULONG)handle != owner && probe_session_query(owner) == EACCDN);
        if (argv[1][0] == 'o') {
            WORD x, y;
            objc_offset((OBJECT *)0, 1, &x, &y);
            return 0x3000;
        }
        if (argv[1][0] == 'f') __asm__ volatile ("ud2");
        if (argv[1][0] == 'l') _Exit(77);
        return 77;             /* libaes destructor closes the child session */
    }
    for (i = 0; i < 8; i++) tail[2 + i] = digits[((ULONG)handle >> ((7 - i) * 4)) & 15];
    for (i = 0; i < 8; i++) tail[10 + i] = digits[((ULONG)window >> ((7 - i) * 4)) & 15];
    for (i = 0; i < 4; i++) {
        tail[1] = i == 0 ? 'c' : i == 1 ? 'l' : i == 2 ? 'f' : 'o';
        CHECK(client_pexec(argv[0], tail, "") == (i >= 2 ? 0xffff : 77));
        {
            PROBE_PACKET packet;
            CHECK(probe_packet_read(handle,&packet) == sizeof(packet));
            CHECK(packet.endpoint > handle && packet.word[0] == 1 && packet.word[1] == 2 && packet.word[2] == 3 && packet.word[3] == 4);
            CHECK(probe_packet_wait(handle,0) == CUIS_READY_TIMER);
        }
        CHECK(probe_session_query(handle) == E_OK);
        CHECK(probe_window_focused(window) == 1);
        CHECK(probe_update_begin(window) == E_OK);
        CHECK(probe_update_end(window) == E_OK);
        CHECK(probe_window_configure(window, -10, 20, 100, 80) == E_OK);
    }
    rc = message_checks(handle, window);
    if (rc) return rc;
    rc = key_checks(window);
    if (rc) return rc;
    rc = pointer_checks();
    if (rc) return rc;
    rc = libaes_drawing_checks();
    if (rc) return rc;
    CHECK(probe_update_begin(window) == E_OK);
    tail[1] = 'u';
    CHECK(client_pexec(argv[0], tail, "") == 77);
    CHECK(probe_update_end(window) == E_OK);
    {
        PROBE_PACKET packet;
        CHECK(probe_packet_read(handle,&packet) == sizeof(packet));
        CHECK(packet.endpoint > handle && packet.word[0] == 1 && packet.word[1] == 2 && packet.word[2] == 3 && packet.word[3] == 4);
    }
    CHECK(probe_window_destroy(window) == E_OK);
    CHECK(probe_window_destroy(window) == EACCDN);
    window = probe_window_create(0, 0, 10, 10);
    CHECK(window >= 0);
    CHECK(probe_session_close(handle) == E_OK);
    CHECK(probe_window_destroy(window) == EACCDN);
    CHECK(probe_session_close(handle) == EACCDN);
    CHECK(probe_session_query(handle) == EACCDN);
    CHECK(probe_session_open() > handle);
    CHECK(probe_session_query(handle) == EACCDN);
    CHECK(probe_window_destroy(window) == EACCDN);
    window = probe_window_create(0, 0, 10, 10);
    CHECK(window >= 0 && probe_window_show(window) == E_OK);
    CHECK(probe_call6(CUIS_WINDOW_POLICY,window,1,0,0,0,0) == E_OK);
    CHECK(probe_window_raise(window) == E_OK);
    CHECK(probe_update_begin(window) == E_OK);
    /* Leave the reopened session to library finalization. */
    {
        static const char pass[] = "x32 libaes objects: PASS\r\nx32 CUIS visibility: PASS\r\nx32 CUIS clipping: PASS\r\nx32 CUIS focus: PASS\r\nx32 CUIS keyboard: PASS\r\nx32 CUIS pointer/events: PASS\r\nx32 CUIS messages: PASS\r\nx32 libaes/CUIS sessions: PASS\r\n";
        CHECK(ptos_write(1, pass, sizeof(pass) - 1) == sizeof(pass) - 1);
    }
    return 77;
}
