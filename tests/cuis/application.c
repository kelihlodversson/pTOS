/* Exercise the public GEM bindings against a failing/private transport. */
#include "gem.h"
#include "libaes.h"
#include "gemerror.h"
#include "../../lib/aes/transport.h"
#define CHECK(c) do { if (!(c)) { __builtin_printf("GEM API failure: %d\n", __LINE__); return __LINE__; } } while (0)
static LONG pid = 7, handle = 2147483600L, failure, yield_result, close_result, label_result;
static BOOL live;
static ULONG starts, closes, yields, delays;
static UQUAD interval;
static UBYTE pipe_data[64];
static ULONG pipe_count, pipe_block;
static LONG wait_error, state_error, read_error;
static LONG state_values[4] = { 17, 23, 2, 5 };
static ULONG wait_calls;
static UQUAD waited_interval, waited_mask;
static LONG native_windows[64], next_native = 2147483000L;
static ULONG window_error_fn;
static LONG window_error;
static ULONG window_calls, display_nesting;
static LONG display_error, region_error;
static LONG region_values[3][4];
static ULONG region_count;
static LONG window_transport(ULONG fn, UQUAD a)
{
    short i;
    window_calls++;
    if (fn == window_error_fn) return window_error;
    if (fn == CUIS_WINDOW_CREATE) {
        for (i = 0; i < 64 && native_windows[i]; i++) ;
        if (i == 64) return ENSMEM;
        return native_windows[i] = next_native++;
    }
    for (i = 0; i < 64 && native_windows[i] != (LONG)a; i++) ;
    if (i == 64) return EACCDN;
    if (fn == CUIS_WINDOW_IDENTIFY) return i + 1;
    if (fn == CUIS_WINDOW_DESTROY) native_windows[i] = 0;
    return E_OK;
}
LONG aes_process_call(ULONG fn)
{
    if (fn == 0x10b) return pid;
    if (fn == 0xff) { yields++; return yield_result; }
    return EINVFN;
}
LONG cuis_call(ULONG fn, LONG id)
{
    if (fn == CUIS_SESSION_START) {
        starts++;
        if (failure) return failure;
        live = TRUE; return handle;
    }
    if (!live || id != handle) return EACCDN;
    if (fn == CUIS_SESSION_QUERY) return E_OK;
    if (fn == CUIS_SESSION_END) {
        if (close_result) return close_result;
        display_nesting = 0;
        __builtin_memset(native_windows, 0, sizeof(native_windows));
        live = FALSE; closes++; return E_OK;
    }
    return EINVFN;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    if (fn == CUIS_WINDOW_REGION) {
        if (region_error) return region_error;
        if (b >= region_count) return 0;
        *(LONG *)(uintptr_t)c = region_values[b][0]; *(LONG *)(uintptr_t)d = region_values[b][1];
        *(LONG *)(uintptr_t)e = region_values[b][2]; *(LONG *)(uintptr_t)f = region_values[b][3];
        return 1;
    }
    if (fn == CUIS_DISPLAY_LOCK) {
        if (display_error) return display_error;
        if (a != (UQUAD)handle || !live) return EACCDN;
        if (b) display_nesting++;
        else if (!display_nesting) return EACCDN;
        else display_nesting--;
        return E_OK;
    }
    if ((fn >= CUIS_WINDOW_CREATE && fn <= CUIS_FOCUS_RELEASE) || (fn == CUIS_WINDOW_IDENTIFY || fn == CUIS_WINDOW_OUTSET))
        return window_transport(fn, a);
    if (fn == CUIS_SESSION_NAME) {
        __builtin_memcpy((void *)(uintptr_t)b, "GemTest.Prg", 12); return 11;
    }
    if (fn == CUIS_SESSION_LABEL) return label_result;
    if (fn == CUIS_LABEL_FIND) return __builtin_memcmp((void *)(uintptr_t)a, "GEMTEST ", 8) ? EFILNF : handle;
    if (fn == CUIS_PEER_BY_PID) return a == (UQUAD)pid ? handle : EFILNF;
    if (fn == CUIS_PEER_PID) return a == (UQUAD)handle ? pid : EFILNF;
    if (fn == CUIS_SESSION_WAIT) {
        LONG ready = 0;
        wait_calls++; waited_mask = c; waited_interval = d;
        if (wait_error) return wait_error;
        if ((c & CUIS_READY_STREAM) && pipe_count >= b) ready |= CUIS_READY_STREAM;
        if (c & CUIS_READY_TIMER) ready |= CUIS_READY_TIMER;
        return ready;
    }
    if (fn == CUIS_INPUT_STATE) {
        if (state_error) return state_error;
        *(LONG *)(uintptr_t)b = state_values[0]; *(LONG *)(uintptr_t)c = state_values[1];
        *(LONG *)(uintptr_t)d = state_values[2]; *(LONG *)(uintptr_t)e = state_values[3];
        return E_OK;
    }
    if (fn == CUIS_STREAM_WRITE || fn == CUIS_STREAM_READ) {
        if (failure) return failure;
        pipe_block = (ULONG)d;
        if (a != (UQUAD)handle) return EACCDN;
        if (fn == CUIS_STREAM_WRITE) {
            if (pipe_count + c > sizeof(pipe_data)) return EAGAIN;
            if (c) __builtin_memcpy(pipe_data + pipe_count, (const void *)(uintptr_t)b, (ULONG)c);
            pipe_count += (ULONG)c;
        } else {
            if (read_error) return read_error;
            if (pipe_count < c) return EAGAIN;
            if (c) __builtin_memcpy((void *)(uintptr_t)b, pipe_data, (ULONG)c);
            pipe_count -= (ULONG)c;
            __builtin_memmove(pipe_data, pipe_data + c, pipe_count);
        }
        return (LONG)c;
    }
    if (fn != CUIS_SESSION_DELAY || a != (UQUAD)handle) return EINVAL;
    delays++; interval = b;
    return failure;
}

#define MULTI(mask, time, msg) evnt_multi(mask, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, msg, time, &x, &y, &buttons, &mods, &key, &clicks)
static int event_checks(void)
{
    short x = 99, y = 99, buttons = 99, mods = 99, key = 99, clicks = 99;
    short message[8] = { 1, 7, 0, 4, 5, 6, 7, 8 }, output[8], global[15];
    ULONG before;
    CHECK(mt_appl_init(global) == 7);
    CHECK(graf_mkstate(&x, &y, &buttons, &mods) == 1 && x == 17 && y == 23 && buttons == 2 && mods == 5);
    CHECK(mt_graf_mkstate(NULL, NULL, NULL, NULL, global) == 1);
    state_values[0] = 40000; state_values[1] = -40000;
    CHECK(graf_mkstate(&x, &y, &buttons, &mods) == 1 && x == 32767 && y == -32768);
    state_values[0] = 17; state_values[1] = 23;
    CHECK(MULTI(MU_TIMER, 0, NULL) == MU_TIMER && waited_mask == CUIS_READY_TIMER && key == 0 && clicks == 0);
    CHECK(appl_write(7, 16, message) == 1);
    before = wait_calls;
    CHECK(MULTI(MU_KEYBD | MU_MESAG, 0, output) == 0 && wait_calls == before && pipe_count == 16);
    CHECK(MULTI(0, 0, output) == 0 && MULTI(MU_MESAG, 0, NULL) == 0 && pipe_count == 16);
    CHECK(MULTI(MU_TIMER, 0xffffffffUL, NULL) == MU_TIMER && waited_interval == 0xffffffffULL && pipe_count == 16);
    if (sizeof(unsigned long) > 4) CHECK(MULTI(MU_TIMER, (unsigned long)(1ULL << 32), NULL) == 0);
    wait_error = EACCDN; CHECK(MULTI(MU_MESAG | MU_TIMER, 0, output) == 0 && pipe_count == 16); wait_error = 0;
    state_error = EIMBA; x = 123;
    CHECK(MULTI(MU_MESAG | MU_TIMER, 0, output) == 0 && pipe_count == 16 && x == 123);
    state_error = 0; read_error = EIMBA;
    CHECK(MULTI(MU_MESAG | MU_TIMER, 0, output) == 0 && pipe_count == 16 && x == 123);
    read_error = 0;
    CHECK(MULTI(MU_MESAG | MU_TIMER, 0, output) == (MU_MESAG | MU_TIMER) && !pipe_count);
    CHECK(!__builtin_memcmp(message, output, 16) && x == 17 && y == 23 && buttons == 2 && mods == 5 && !key && !clicks);
    CHECK(appl_write(7, 16, message) == 1);
    CHECK(mt_evnt_multi(MU_MESAG, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        output, ~0UL, NULL, NULL, NULL, NULL, NULL, NULL, global) == MU_MESAG);
    CHECK(!pipe_count && waited_mask == CUIS_READY_STREAM);
    return 0;
}
#undef MULTI
#include "window_api.c"
static int window_failures(void)
{
    short id, x = 99, y = 99, w = 99, h = 99;
    window_error_fn = CUIS_WINDOW_CREATE; window_error = ENSMEM;
    CHECK(wind_create(0, 0, 0, 10, 10) == -1);
    window_error_fn = CUIS_WINDOW_IDENTIFY; window_error = EACCDN;
    CHECK(wind_create(0, 0, 0, 10, 10) == -1 && native_windows[0] == 0);
    window_error = 32768;
    CHECK(wind_create(0, 0, 0, 10, 10) == -1 && native_windows[0] == 0);
    window_error_fn = 0;
    id = wind_create(0, 0, 0, 10, 10); CHECK(id > 0);
    window_error_fn = CUIS_WINDOW_SHOW; window_error = EAGAIN;
    CHECK(wind_open(id, 1, 2, 10, 20) == 0);
    CHECK(wind_get(id, WF_CURRXYWH, &x, &y, &w, &h) == 1 && !(x | y | w | h));
    window_error_fn = 0; CHECK(wind_open(id, 1, 2, 10, 20) == 1);
    window_error_fn = CUIS_WINDOW_CONFIGURE; window_error = EAGAIN;
    CHECK(wind_set(id, WF_CURRXYWH, 5, 6, 30, 40) == 0);
    CHECK(wind_get(id, WF_CURRXYWH, &x, &y, &w, &h) == 1 && x == 1 && y == 2 && w == 10 && h == 20);
    window_error_fn = CUIS_WINDOW_HIDE;
    CHECK(wind_close(id) == 0);
    window_error_fn = CUIS_WINDOW_DESTROY;
    CHECK(wind_delete(id) == 0);
    window_error_fn = 0;
    CHECK(wind_close(id) == 1 && wind_delete(id) == 1);
    return 0;
}
static int display_api_checks(void)
{
    short id, other, x = 99, y, w, h;
    CHECK(wind_update(END_UPDATE) == 0);
    CHECK(wind_update(BEG_MCTRL) == 0 && wind_update(END_MCTRL) == 0 && wind_update(-1) == 0);
    CHECK(wind_update(BEG_UPDATE | NO_BLOCK) == 1 && wind_update(BEG_UPDATE) == 1);
    CHECK(display_nesting == 2);
    id = wind_create(0, 0, 0, 100, 100);
    other = wind_create(0, 0, 0, 100, 100);
    CHECK(id > 0 && other > 0 && display_nesting == 2);
    CHECK(wind_open(id, 0, 0, 20, 20) == 1 && wind_open(other, 0, 0, 20, 20) == 1);
    CHECK(display_nesting == 2 && wind_update(END_UPDATE) == 1 && display_nesting == 1);
    region_values[0][0] = 0; region_values[0][1] = 0; region_values[0][2] = 1; region_values[0][3] = 20;
    region_values[1][0] = 0; region_values[1][1] = 0; region_values[1][2] = 10; region_values[1][3] = 10;
    region_values[2][0] = 10; region_values[2][1] = 0; region_values[2][2] = 10; region_values[2][3] = 20;
    region_count = 3;
    CHECK(wind_get(id, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && x == 1 && y == 1 && w == 9 && h == 9);
    CHECK(wind_get(other, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && x == 1);
    region_error = EIMBA; x = 99;
    CHECK(wind_get(id, WF_NEXTXYWH, &x, &y, &w, &h) == 0 && x == 99);
    region_error = 0;
    CHECK(wind_get(id, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && x == 10 && y == 1 && w == 9 && h == 18);
    CHECK(wind_get(other, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && x == 10);
    CHECK(wind_get(id, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    CHECK(wind_get(id, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    CHECK(wind_get(id, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && x == 1);
    CHECK(wind_delete(id) == 1 && wind_delete(other) == 1 && display_nesting == 1);
    CHECK(wind_update(END_UPDATE) == 1 && !display_nesting);
    display_error = EAGAIN;
    CHECK(wind_update(BEG_UPDATE | NO_BLOCK) == 0 && wind_create(0, 0, 0, 20, 20) == -1);
    display_error = 0; region_count = 0;
    return 0;
}
int main(void)
{
    short other[AES_GLOBMAX], a = 1, b = 2, c = 3, d = 4;
    short message[8] = { 123, 7, 0, 4, 5, 6, 7, 8 }, result[8];
    ULONG count;
    short held;
    CHECK(gl_apid == -1 && aes_global[2] == -1);
    CHECK(wind_create(0, 0, 0, 100, 100) == -1 && !window_calls);
    CHECK(wind_get(1, WF_KIND, &a, &b, &c, &d) == 0 && a == 1 && !window_calls);
    CHECK(appl_exit() == 0 && appl_yield() == 0 && evnt_timer(0) == 0);
    CHECK(mt_appl_init(NULL) == -1);
    pid = 32768; CHECK(appl_init() == -1 && !starts && gl_apid == -1);
    pid = -1; CHECK(appl_init() == -1 && !starts);
    pid = 7; failure = ENSMEM;
    CHECK(appl_init() == -1 && gl_ap_version == 0 && !live);
    failure = 0;
    CHECK(appl_init() == 7 && gl_apid == 7 && _AESapid == 7 && _AESnumapps == -1);
    CHECK(gl_ap_version == aes_global[0] && gl_ap_version == 0x0400);
    CHECK(mt_appl_init(other) == 7 && other[2] == 7 && gl_apid == 7);
    CHECK(appl_init() == 7 && closes == 0); /* Repeated registration is idempotent. */
    CHECK(appl_yield() == 1 && yields == 1);
    yield_result = EACCDN; CHECK(mt_appl_yield(other) == 0);
    yield_result = 0;
    CHECK(evnt_timer(0) == 1 && delays == 1 && interval == 0);
    CHECK(mt_evnt_timer(0xffffffffUL, other) == 1 && interval == 0xffffffffULL);
    count = delays;
    if (sizeof(unsigned long) > 4) CHECK(evnt_timer((unsigned long)(1ULL << 32)) == 0 && delays == count);
    failure = EACCDN; CHECK(evnt_timer(1) == 0); failure = 0;
    CHECK(appl_getinfo(32767, &a, &b, &c, &d) == 0 && !(a | b | c | d));
    CHECK(appl_getinfo(AES_PROCESS, &a, &b, &c, &d) == 1 && a == 0 && b == 1 && c == 0 && d == 0);
    CHECK(appl_getinfo(AES_INQUIRE, &a, &b, &c, &d) == 1 && a == 1 && b == 0 && c == 0 && d == 0);
    CHECK(appl_find(NULL) == 7 && appl_find("gemtest") == 7 && mt_appl_find("GEMTEST ", other) == 7);
    CHECK(appl_find("missing") == -1 && appl_find((const char *)(uintptr_t)0xffff0007UL) == 7);
    CHECK(appl_find((const char *)(uintptr_t)0xfffe0007UL) == 7);
    CHECK(appl_find((const char *)(uintptr_t)0xffff0008UL) == -1);
    CHECK(appl_read(APR_NOWAIT, 16, result) == 0 && pipe_block == 0);
    CHECK(appl_read(8, 16, result) == 0 && appl_write(8, 16, message) == 0);
    CHECK(appl_write(7, -1, message) == 0 && appl_read(7, -1, result) == 0);
    CHECK(appl_write(7, 0, NULL) == 1 && appl_read(7, 0, NULL) == 1);
    CHECK(mt_appl_write(7, 16, message, other) == 1 && pipe_block == 1);
    CHECK(mt_evnt_mesag(result, other) == 1 && pipe_block == 1);
    CHECK(!__builtin_memcmp(result, message, sizeof(message)));
    CHECK(appl_write(7, 16, message) == 1 && appl_read(APR_NOWAIT, 16, result) == 1 && pipe_block == 0);
    failure = EACCDN;
    CHECK(appl_write(7, 16, message) == 0 && evnt_mesag(result) == 0);
    failure = 0;
    CHECK(event_checks() == 0);
    CHECK(gem_window_checks(other) == 0 && window_failures() == 0 && display_api_checks() == 0);
    CHECK(mt_appl_getinfo(-1, NULL, NULL, NULL, NULL, other) == 0);
    other[2] = 8; CHECK(mt_appl_exit(other) == 0 && live); other[2] = 7;
    held = wind_create(0, 0, 0, 10, 10); CHECK(held > 0);
    close_result = EACCDN;
    CHECK(appl_exit() == 0 && live && gl_apid == 7);
    CHECK(wind_get(held, WF_KIND, NULL, NULL, NULL, NULL) == 1);
    close_result = 0;
    CHECK(mt_appl_exit(other) == 1 && !live && closes == 1 && other[2] == -1);
    CHECK(gl_apid == -1 && aes_global[2] == -1 && gl_ap_version == 0);
    CHECK(mt_appl_yield(other) == 0 && appl_exit() == 0);
    CHECK(appl_init() == 7 && live);
    CHECK(wind_get(held, WF_KIND, NULL, NULL, NULL, NULL) == 0);
    live = FALSE; /* Authoritative kernel teardown invalidates stale library state. */
    CHECK(appl_yield() == 0 && evnt_timer(0) == 0);
    CHECK(appl_init() == 7 && appl_exit() == 1 && closes == 2);
    pid = 32767;
    CHECK(appl_init() == 32767 && gl_apid == 32767 && appl_exit() == 1 && closes == 3);
    label_result = EACCDN;
    CHECK(mt_appl_init(other) == -1 && !live && gl_apid == -1 && other[2] == -1 && closes == 4);
    return 0;
}
