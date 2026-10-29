/* Exercise the public GEM bindings against a failing/private transport. */
#include "gem.h"
#include "libaes.h"
#include "gemerror.h"
#include "../../lib/aes/transport.h"
#include "../../lib/aes/application.h"
#include "../../lib/aes/frames.h"
#include "gemmovegeom.h"
#include "gemsizegeom.h"
#include "../../lib/aes/layout.h"
#undef CHECK
#define CHECK(c) do { if (!(c)) { __builtin_printf("GEM API failure: %d\n", __LINE__); return __LINE__; } } while (0)
extern void *malloc(__SIZE_TYPE__ bytes);
extern void free(void *memory);
static BOOL region_alloc_failure;
void *aes_temporary_alloc(LONG bytes) { return region_alloc_failure ? NULL : malloc(bytes); }
WORD aes_resource_discard(void *memory) { free(memory); return 0; }
static LONG pid = 7, handle = 2147483600L, failure, yield_result, close_result, label_result;
static BOOL live;
static ULONG starts, closes, yields, delays;
static UQUAD interval;
static UBYTE pipe_data[64];
static ULONG pipe_count, pipe_block;
static LONG wait_error, state_error, read_error;
static LONG state_values[4] = { 17, 23, 2, 5 };
static ULONG wait_calls, menu_ticks, menu_dispatch_ticks, menu_wakes;
static BOOL menu_test;
static LONG input_ready, input_key = 0x001e0061, input_count = 2;
static UQUAD input_arguments[5];
static ULONG input_configuration, click_interval = 450;
static UQUAD waited_interval, waited_mask;
static LONG native_windows[64], next_native = 2147483000L;
static ULONG window_error_fn;
static LONG window_error;
static ULONG window_calls, display_nesting;
static LONG display_error, region_error, mouse_error;
static ULONG display_block_requests;
static BOOL release_display_on_wait;
static ULONG display_release_ticks;
static ULONG mouse_nesting, mouse_block;
static LONG region_values[3][4];
static ULONG region_count;
static LONG stack_values[4];
static LONG notice_kind, notice_native, notice_id, publish_failure, notice_on_wait;
static ULONG notice_only_wakes;
static LONG control_events[32][4], control_target, control_read_error;
static ULONG control_count, control_at, control_only_wakes;
static BOOL control_selection;
static WORD control_object;
static ULONG move_calls, size_calls;
static void control_event(LONG x, LONG y, LONG buttons, LONG changed)
{
    control_events[control_count][0] = x; control_events[control_count][1] = y;
    control_events[control_count][2] = buttons; control_events[control_count++][3] = changed;
}
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
static int resource_resets;
void aes_resources_reset(void) { resource_resets++; }
BOOL aes_menu_registered(void) { return menu_test; }
BOOL aes_menu_listening(void) { return menu_test; }
LONG aes_menu_flush(WORD *global) { (void)global; return E_OK; }
LONG aes_menu_dispatch(WORD *global)
{
    (void)global;
    if (menu_test) { menu_ticks += menu_dispatch_ticks; menu_dispatch_ticks = 0; }
    return E_OK;
}
void aes_menu_reset(void) { }
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
        display_nesting = mouse_nesting = 0;
        __builtin_memset(native_windows, 0, sizeof(native_windows));
        live = FALSE; closes++; return E_OK;
    }
    return EINVFN;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    if (fn == CUIS_INPUT_BUTTON || fn == CUIS_INPUT_RECT) {
        input_configuration = fn;
        input_arguments[0] = b; input_arguments[1] = c; input_arguments[2] = d;
        input_arguments[3] = e; input_arguments[4] = f;
        return failure;
    }
    if (fn == CUIS_INPUT_INTERVAL) {
        if (c) click_interval = (ULONG)b;
        return (LONG)click_interval;
    }
    if (fn == CUIS_INPUT_RESULT) {
        if (state_error) return state_error;
        *(LONG *)(uintptr_t)b = input_key; *(LONG *)(uintptr_t)c = input_count;
        *(LONG *)(uintptr_t)d = state_values[0]; *(LONG *)(uintptr_t)e = state_values[1];
        *(LONG *)(uintptr_t)f = state_values[2] | (state_values[3] << 16);
        return E_OK;
    }
    if (fn == CUIS_STACK_QUERY || fn == CUIS_WINDOW_INFO) {
        *(LONG *)(uintptr_t)b = stack_values[0]; *(LONG *)(uintptr_t)c = stack_values[1];
        *(LONG *)(uintptr_t)d = stack_values[2];
        if (fn == CUIS_WINDOW_INFO) *(LONG *)(uintptr_t)e = stack_values[3];
        return E_OK;
    }
    if (fn == CUIS_NOTICE_PEEK) {
        if (notice_kind) { *(LONG *)(uintptr_t)b = notice_native; *(LONG *)(uintptr_t)c = notice_id; }
        return notice_kind;
    }
    if (fn == CUIS_NOTICE_PUBLISH) {
        if (publish_failure) return publish_failure;
        if (b != (UQUAD)notice_native || c != (UQUAD)notice_kind) return EACCDN;
        if (pipe_count + e > sizeof(pipe_data)) return EAGAIN;
        __builtin_memcpy(pipe_data + pipe_count, (const void *)(uintptr_t)d, e);
        pipe_count += e; notice_kind = 0; return E_OK;
    }
    if (fn == CUIS_CONTROL_READ) {
        if ((LONG)a != control_target) return 0;
        if (control_read_error) { LONG result = control_read_error; control_read_error = 0; return result; }
        if (control_at == control_count) return 0;
        *(LONG *)(uintptr_t)b = control_events[control_at][0];
        *(LONG *)(uintptr_t)c = control_events[control_at][1];
        *(LONG *)(uintptr_t)d = control_events[control_at][2];
        *(LONG *)(uintptr_t)e = control_events[control_at++][3];
        return 1;
    }
    if (fn == CUIS_WINDOW_REGION) {
        if (region_error) return region_error;
        if (b >= region_count) return 0;
        *(LONG *)(uintptr_t)c = region_values[b][0]; *(LONG *)(uintptr_t)d = region_values[b][1];
        *(LONG *)(uintptr_t)e = region_values[b][2]; *(LONG *)(uintptr_t)f = region_values[b][3];
        return 1;
    }
    if (fn == CUIS_MOUSE_LOCK) {
        mouse_block = c;
        if (mouse_error) return mouse_error;
        if (a != (UQUAD)handle || !live) return EACCDN;
        if (b) mouse_nesting++;
        else if (!mouse_nesting) return EACCDN;
        else mouse_nesting--;
        return E_OK;
    }
    if (fn == CUIS_DISPLAY_LOCK) {
        if (b && c) display_block_requests++;
        if (display_error) return display_error;
        if (a != (UQUAD)handle || !live) return EACCDN;
        if (b) display_nesting++;
        else if (!display_nesting) return EACCDN;
        else display_nesting--;
        return E_OK;
    }
    if ((fn >= CUIS_WINDOW_CREATE && fn <= CUIS_FOCUS_RELEASE) || (fn == CUIS_WINDOW_IDENTIFY || fn == CUIS_WINDOW_OUTSET || fn == CUIS_WINDOW_POLICY || fn == CUIS_WINDOW_CONTENT))
        return window_transport(fn, a);
    if (fn == CUIS_SESSION_NAME) {
        __builtin_memcpy((void *)(uintptr_t)b, "GemTest.Prg", 12); return 11;
    }
    if (fn == CUIS_SESSION_LABEL) return label_result;
    if (fn == CUIS_LABEL_FIND) return __builtin_memcmp((void *)(uintptr_t)a, "GEMTEST ", 8) ? EFILNF : handle;
    if (fn == CUIS_PEER_BY_PID) return a == (UQUAD)pid ? handle : EFILNF;
    if (fn == CUIS_PEER_PID) return a == (UQUAD)handle ? pid : EFILNF;
    if (fn == CUIS_CLOCK) return menu_ticks & 0x7fffffffUL;
    if (fn == CUIS_SESSION_WAIT) {
        LONG ready = input_ready & c;
        wait_calls++; waited_mask = c; waited_interval = d;
        if (menu_test && menu_wakes && (c & CUIS_READY_SURFACE)) {
            menu_wakes--; menu_ticks++; return CUIS_READY_SURFACE;
        }
        if (release_display_on_wait && (c & CUIS_READY_DISPLAY)) {
            release_display_on_wait = FALSE; display_error = 0;
            if (display_release_ticks) {
                menu_ticks += display_release_ticks; display_release_ticks = 0;
                return CUIS_READY_DISPLAY;
            }
        }
        if ((c & CUIS_READY_CONTROL) && control_only_wakes && d) {
            control_only_wakes--; menu_ticks += 3;
            control_event(25,35,0,1);
            return CUIS_READY_CONTROL;
        }
        if ((c & CUIS_READY_CONTROL) && (control_at < control_count || control_read_error)) ready |= CUIS_READY_CONTROL;
        if ((c & CUIS_READY_DISPLAY) && !display_error) ready |= CUIS_READY_DISPLAY;
        if (notice_on_wait) { notice_kind = notice_on_wait; notice_on_wait = 0; }
        if ((c & CUIS_READY_NOTICE) && notice_kind) {
            ready |= CUIS_READY_NOTICE;
            if (notice_only_wakes) { notice_only_wakes--; menu_ticks += 3; return ready; }
        }
        if (wait_error) return wait_error;
        if ((c & CUIS_READY_STREAM) && pipe_count >= b) ready |= CUIS_READY_STREAM;
        if (c & CUIS_READY_TIMER) {
            if (menu_test && d) menu_ticks += d/5 + (d%5 != 0) + 1;
            ready |= CUIS_READY_TIMER;
        }
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
    CHECK(MULTI(0x40 | MU_MESAG, 0, output) == 0 && wait_calls == before && pipe_count == 16);
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
    CHECK(!pipe_count && waited_mask == (CUIS_READY_STREAM | CUIS_READY_NOTICE));
    return 0;
}
static int input_api_checks(void)
{
    short x, y, buttons, mods, key, clicks, global[15], message[8] = { 0 };
    ULONG before;
    CHECK(mt_appl_init(global) == 7);
    input_ready = CUIS_READY_KEY;
    CHECK(evnt_keybd() == 0x1e61 && mt_evnt_keybd(global) == 0x1e61);
    input_key = 0x00ff00ff;
    CHECK((UWORD)evnt_keybd() == 0xffff); input_key = 0x001e0061;
    input_ready = CUIS_READY_BUTTON;
    CHECK(evnt_button(0x102, 3, 1, &x, &y, &buttons, &mods) == 2);
    CHECK(input_configuration == CUIS_INPUT_BUTTON && input_arguments[0] == 3 &&
          input_arguments[1] == 1 && input_arguments[2] == 2 && input_arguments[3] == 1);
    CHECK(mt_evnt_button(1, 1, 0, NULL, NULL, NULL, NULL, global) == 2);
    input_ready = CUIS_READY_RECT1;
    CHECK(evnt_mouse(1, -3, -5, 7, 9, &x, &y, &buttons, &mods) == 1);
    CHECK(input_configuration == CUIS_INPUT_RECT && input_arguments[0] == 2 &&
          input_arguments[1] == (UQUAD)(QUAD)-3 && input_arguments[2] == (UQUAD)(QUAD)-5);
    CHECK(mt_evnt_mouse(0, 0, 0, 1, 1, NULL, NULL, NULL, NULL, global) == 1);
    CHECK(evnt_dclick(4, 1) == 4 && mt_evnt_dclick(-1, 0, global) == 4 && evnt_dclick(0, 1) == 0);
    input_ready = CUIS_READY_INPUT;
    CHECK(appl_write(7, 16, message) == 1);
    before = wait_calls;
    CHECK(MULTI(63, 9, message) == 63 && wait_calls == before + 1 && waited_mask == (CUIS_READY_ALL & ~(CUIS_READY_SURFACE|CUIS_READY_DISPLAY|CUIS_READY_CONTROL)));
    CHECK(!pipe_count && waited_interval == 9 && key == 0x1e61 && clicks == 2);
    CHECK(x == 17 && y == 23 && buttons == 2 && mods == 5);
    state_error = EIMBA; x = 123;
    CHECK(MULTI(MU_KEYBD | MU_TIMER, 0, NULL) == 0 && x == 123); state_error = 0;
    before = wait_calls;
    CHECK(evnt_mouse(2,0,0,1,1,NULL,NULL,NULL,NULL) == 0 && wait_calls == before);
    CHECK(evnt_button(0x201,1,1,NULL,NULL,NULL,NULL) == 0 && wait_calls == before);
    failure = EACCDN;
    CHECK(evnt_button(1,1,1,NULL,NULL,NULL,NULL) == 0 && wait_calls == before);
    failure = 0; input_ready = 0;
    menu_test = TRUE; menu_ticks = 0; menu_dispatch_ticks = 1; menu_wakes = 1;
    before = wait_calls;
    CHECK(MULTI(MU_TIMER,10,NULL) == MU_TIMER && wait_calls == before+2);
    CHECK(menu_ticks >= 3 && waited_interval == 1 && waited_mask == (CUIS_READY_TIMER|CUIS_READY_SURFACE));
    menu_ticks = 0x7fffffffUL; menu_dispatch_ticks = 1; menu_wakes = 0;
    CHECK(MULTI(MU_TIMER,10,NULL) == MU_TIMER && waited_interval == 5);
    CHECK(((menu_ticks-0x7fffffffUL) & 0x7fffffffUL) >= 3);
    menu_test = FALSE;
    return 0;
}
#undef MULTI
/* Deterministic native system-font box metrics for the window binding. */
static BOOL frame_metrics_failure;
static WORD frame_screen_width = 640, frame_screen_height = 400;
BOOL aes_frame_metrics(WORD *wbox, WORD *hbox, WORD *width, WORD *height, WORD *global)
{
    if (frame_metrics_failure || aes_application_session(global) < 0) return FALSE;
    *wbox = 19; *hbox = 19;
    if (width) *width = frame_screen_width;
    if (height) *height = frame_screen_height;
    return TRUE;
}
BOOL aes_layout_metrics(LAYOUT_METRICS *metrics, WORD *global)
{
    (void)global;
    if (frame_metrics_failure) return FALSE;
    metrics->width = frame_screen_width; metrics->height = frame_screen_height;
    metrics->wchar = 8; metrics->hchar = 16; metrics->hbox = 19;
    return TRUE;
}
/* Window lifecycle tests isolate the private paint adapter; pixel and native
 * frame construction contracts run against production code in other suites. */
static BOOL frame_paint_failure;
static ULONG frame_paints;
static BOOL frame_damage_once;
static ULONG frame_paint_ticks;
static char *last_frame_name;
BOOL aes_frame_prepare(WM_FRAME_STATE *state, UWORD kind, const GRECT *border,
                       char *name, char *info, WORD *global)
{
    (void)kind; (void)global;
    if (border->g_w < 5 || border->g_h < 50) return FALSE;
    state->kind = kind; state->wbox = 19; state->hbox = 19;
    state->border = *border; state->name = name; state->info = info;
    return TRUE;
}
BOOL aes_frame_paint(LONG native, const WM_FRAME_STATE *state, WORD *global)
{
    (void)native; (void)global;
    if (!state->name || !state->info) return FALSE;
    frame_paints++; last_frame_name = state->name;
    menu_ticks += frame_paint_ticks; frame_paint_ticks = 0;
    if (frame_damage_once) { frame_damage_once = FALSE; notice_kind = CUIS_NOTICE_REDRAW; }
    return !frame_paint_failure;
}
BOOL aes_frame_paint_control(LONG native, const WM_FRAME_STATE *state, WORD object, BOOL selected, WORD *global)
{
    control_object = object; control_selection = selected;
    return aes_frame_paint(native,state,global);
}
WORD aes_frame_control(const WM_FRAME_STATE *state, LONG x, LONG y)
{
    if ((state->kind & SIZER) && x >= state->border.g_x+state->border.g_w-20 &&
        x < state->border.g_x+state->border.g_w && y >= state->border.g_y+state->border.g_h-20 &&
        y < state->border.g_y+state->border.g_h) return W_SIZER;
    if (y < state->border.g_y+2 || y >= state->border.g_y+18) return 0;
    if ((state->kind & CLOSER) && x >= state->border.g_x+2 && x < state->border.g_x+18) return W_CLOSER;
    if ((state->kind & FULLER) && x >= state->border.g_x+state->border.g_w-18 && x < state->border.g_x+state->border.g_w-2) return W_FULLER;
    return (state->kind & MOVER) ? W_NAME : 0;
}
BOOL aes_frame_move(LONG native, const WM_FRAME_STATE *state, LONG press_x, LONG press_y, GRECT *out, WORD *global)
{
    LONG event[4], result;
    (void)global; move_calls++;
    for (;;) {
        result = cuis_call6(CUIS_CONTROL_READ,native,(uintptr_t)&event[0],(uintptr_t)&event[1],
            (uintptr_t)&event[2],(uintptr_t)&event[3],0);
        if (result != 1 || (event[2] & ~1L)) return FALSE;
        if (!(event[2] & 1)) return (event[3] & 1) && wm_move_position(&state->border,press_x,press_y,
            event[0],event[1],frame_screen_width,state->wbox,state->hbox,out);
    }
}
BOOL aes_frame_size(LONG native, const WM_FRAME_STATE *state, LONG press_x, LONG press_y, GRECT *out, WORD *global)
{
    LONG event[4], result;
    (void)global; (void)press_x; (void)press_y; size_calls++;
    for (;;) {
        result = cuis_call6(CUIS_CONTROL_READ,native,(uintptr_t)&event[0],(uintptr_t)&event[1],
            (uintptr_t)&event[2],(uintptr_t)&event[3],0);
        if (result != 1 || (event[2] & ~1L)) return FALSE;
        if (!(event[2] & 1)) return (event[3] & 1) && wm_size_position(&state->border,state->kind,
            event[0],event[1],8,16,state->wbox,state->hbox,frame_screen_width,frame_screen_height,out);
    }
}
BOOL aes_frame_control_inside(const WM_FRAME_STATE *state, WORD object, LONG x, LONG y)
{
    /* Model the low-resolution overlap that loses initial tree priority. */
    if (object == W_CLOSER && x == state->border.g_x+22 && y == state->border.g_y+5) return TRUE;
    return aes_frame_control(state,x,y) == object;
}
#include "window_api.c"
static int window_failures(void)
{
    short id, x = 99, y = 99, w = 99, h = 99;
    frame_metrics_failure = TRUE;
    CHECK(wind_calc(WC_WORK,NAME,0,0,100,80,&x,&y,&w,&h) == 0);
    CHECK(x == 99 && y == 99 && w == 99 && h == 99);
    frame_metrics_failure = FALSE;
    CHECK(wind_calc(WC_WORK,NAME|INFO|SIZER,0,0,100,80,&x,&y,&w,&h) == 1);
    CHECK(x == 1 && y == 42 && w == 76 && h == 37);
    frame_screen_width = 320; frame_screen_height = 200;
    CHECK(wind_calc(WC_WORK,NAME|INFO|SIZER,0,0,100,80,&x,&y,&w,&h) == 1);
    CHECK(x == 1 && y == 42 && w == 77 && h == 37);
    CHECK(wind_calc(WC_BORDER,NAME|INFO|SIZER,x,y,w,h,&x,&y,&w,&h) == 1);
    CHECK(x == 0 && y == 0 && w == 100 && h == 80);
    frame_screen_height = 400;
    CHECK(wind_calc(WC_WORK,NAME|INFO|SIZER,0,0,100,80,&x,&y,&w,&h) == 1);
    CHECK(x == 1 && y == 42 && w == 78 && h == 37);
    frame_screen_width = 640;
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
static int frame_failures(void)
{
    short id, x, y, w, h;
    id = wind_create(NAME|INFO,0,0,200,120);
    CHECK(id > 0 && wind_set_str(id,WF_NAME,"Original") == 1);
    frame_paint_failure = TRUE;
    CHECK(wind_open(id,10,20,100,80) == 0 && wind_close(id) == 0);
    CHECK(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && !(x|y|w|h));
    frame_paint_failure = FALSE;
    CHECK(wind_open(id,10,20,100,80) == 1);
    frame_paint_failure = TRUE;
    CHECK(wind_set(id,WF_CURRXYWH,30,40,120,100) == 0);
    CHECK(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && x == 10 && y == 20 && w == 100 && h == 80);
    CHECK(wind_set_str(id,WF_NAME,"Replacement") == 0 && last_frame_name[0] == 'O');
    frame_paint_failure = FALSE;
    CHECK(wind_close(id) == 1 && wind_delete(id) == 1);
    return 0;
}
static int frame_event_checks(void)
{
    short id, message[8], clicks;
    ULONG before;
    id = wind_create(NAME|INFO,0,0,200,120);
    CHECK(id > 0 && wind_open(id,10,20,100,80) == 1);
    notice_native = native_windows[id-1]; notice_id = id;
    notice_only_wakes = 1; notice_on_wait = CUIS_NOTICE_REDRAW;
    menu_ticks = 0; before = wait_calls;
    CHECK(evnt_timer(20) == 1 && wait_calls == before+2 && waited_interval == 5);
    CHECK(!notice_kind && pipe_count == 16 && (waited_mask & CUIS_READY_NOTICE));
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && message[0] == WM_REDRAW && message[3] == id);
    pipe_count = sizeof(pipe_data); notice_kind = CUIS_NOTICE_REDRAW;
    CHECK(evnt_timer(0) == 1 && !(waited_mask & CUIS_READY_NOTICE) && notice_kind);
    pipe_count = 0; notice_kind = 0;
    notice_on_wait = CUIS_NOTICE_REDRAW; input_ready = CUIS_READY_BUTTON; input_count = 2;
    CHECK(evnt_multi(MU_BUTTON,2,1,1,0,0,0,0,0,0,0,0,0,0,NULL,0,
                     NULL,NULL,NULL,NULL,NULL,&clicks) == MU_BUTTON && clicks == 2);
    CHECK(!(waited_mask & CUIS_READY_NOTICE) && !notice_kind && pipe_count == 16);
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && message[0] == WM_REDRAW);
    input_ready = 0;
    notice_kind = CUIS_NOTICE_REDRAW; display_error = EAGAIN;
    before = frame_paints;
    CHECK(appl_read(APR_NOWAIT,16,message) == 0 && frame_paints == before && notice_kind);
    display_error = 0;
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && message[0] == WM_REDRAW);
    CHECK(!display_nesting && frame_paints == before+1);
    notice_kind = CUIS_NOTICE_REDRAW; frame_damage_once = TRUE;
    before = frame_paints;
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && message[0] == WM_REDRAW);
    CHECK(!notice_kind && frame_paints == before+2 && pipe_count == 16);
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && !pipe_count);
    pipe_count = sizeof(pipe_data); notice_kind = CUIS_NOTICE_REDRAW;
    frame_paint_failure = TRUE;
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48 && notice_kind);
    pipe_count = sizeof(pipe_data);
    CHECK(evnt_mesag(message) == 1 && pipe_count == 48 && notice_kind);
    frame_paint_failure = FALSE; pipe_count = 0;

    /* A lock holder may be blocked writing our full stream. Repainting
     * cannot prevent reads from releasing its backpressure. */
    notice_kind = CUIS_NOTICE_REDRAW; display_error = EAGAIN;
    pipe_count = sizeof(pipe_data); before = display_block_requests;
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48 && notice_kind);
    CHECK(evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,0,
                     NULL,NULL,NULL,NULL,NULL,NULL) == (MU_MESAG|MU_TIMER));
    CHECK(pipe_count == 32 && notice_kind && display_block_requests == before);
    pipe_count = 0; release_display_on_wait = TRUE;
    CHECK(appl_read(pid,16,message) == 1 && message[0] == WM_REDRAW);
    CHECK(!notice_kind && !display_error && display_block_requests == before);
    /* Time spent servicing the pre-wait frame must consume the original
     * timer budget, including across the low-31-bit clock wrap. */
    notice_kind = CUIS_NOTICE_REDRAW; menu_ticks = 0; frame_paint_ticks = 10;
    CHECK(evnt_timer(20) == 1 && waited_interval == 0);
    pipe_count = 0; notice_kind = CUIS_NOTICE_REDRAW;
    menu_ticks = 0x7ffffffeUL; frame_paint_ticks = 10;
    CHECK(evnt_timer(20) == 1 && waited_interval == 0);
    pipe_count = 0;
    /* Nested overlays leave damage queued and suppress notice readiness
     * in both pre- and post-wait pumps until the outer scope ends. */
    CHECK(aes_frames_suspend() && aes_frames_suspend());
    notice_kind = CUIS_NOTICE_REDRAW; before = frame_paints;
    CHECK(evnt_timer(0) == 1 && notice_kind && frame_paints == before);
    CHECK(!(waited_mask & (CUIS_READY_NOTICE|CUIS_READY_DISPLAY)));
    CHECK(appl_read(APR_NOWAIT,16,message) == 0 && notice_kind);
    aes_frames_resume();
    CHECK(evnt_timer(0) == 1 && notice_kind && frame_paints == before);
    aes_frames_resume();
    CHECK(evnt_timer(0) == 1 && !notice_kind && frame_paints == before+1);
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && message[0] == WM_REDRAW);
    CHECK(wind_close(id) == 1 && wind_delete(id) == 1);
    return 0;
}
static int control_api_checks(void)
{
    short id, other, message[8], x,y,w,h;
    ULONG before;
    id = wind_create(NAME|CLOSER|FULLER,0,0,200,140);
    CHECK(id > 0 && wind_open(id,20,30,200,140) == 1);
    control_target = native_windows[id-1]; control_at = control_count = 0;
    control_event(42,35,1,1); control_event(42,35,0,1);
    CHECK(evnt_timer(0) == 1 && !pipe_count && !control_selection);
    control_event(25,35,1,1); control_event(42,35,1,0);
    CHECK(evnt_timer(0) == 1 && control_selection);
    control_event(42,35,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_CLOSED);
    control_at = control_count = 0;
    control_event(25,35,1,1);
    CHECK(evnt_timer(0) == 1 && control_selection && control_object == W_CLOSER && !pipe_count);
    control_event(60,75,1,0);
    CHECK(evnt_timer(0) == 1 && !control_selection && !pipe_count);
    control_event(25,35,1,0);
    CHECK(evnt_timer(0) == 1 && control_selection);
    control_event(25,35,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_CLOSED && message[3] == id);
    CHECK(message[1] == 0 && message[2] == 0 && message[4] == 20 && message[5] == 30 &&
          message[6] == 200 && message[7] == 140 && !control_selection);
    CHECK(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && x == 20 && y == 30 && w == 200 && h == 140);
    control_at = control_count = 0;
    control_event(210,35,1,1); control_event(210,35,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_FULLED && message[3] == id);
    CHECK(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && w == 200 && h == 140);
    control_event(25,35,1,1); control_event(60,75,0,1);
    CHECK(evnt_timer(0) == 1 && !pipe_count && !control_selection);
    control_event(25,35,3,3); control_event(25,35,0,3);
    CHECK(evnt_timer(0) == 1 && !pipe_count);
    control_event(25,35,1,1);
    CHECK(evnt_timer(0) == 1 && control_selection);
    control_read_error = ERANGE;
    control_event(25,35,0,1);
    CHECK(evnt_timer(0) == 1 && !pipe_count && !control_selection);
    /* Full self-stream: retain the completed message while reads drain it. */
    control_at = control_count = 0;
    control_event(210,35,1,1); control_event(210,35,0,1);
    pipe_count = sizeof(pipe_data);
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48);
    CHECK(wind_set(id,WF_CURRXYWH,30,40,210,150) == 1);
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48);
    CHECK(evnt_mesag(message) == 1 && evnt_mesag(message) == 1 && evnt_mesag(message) == 1);
    CHECK(message[0] == WM_FULLED && !pipe_count && message[4] == 20 && message[5] == 30 &&
          message[6] == 200 && message[7] == 140);
    CHECK(wind_set(id,WF_CURRXYWH,20,30,200,140) == 1);
    /* Queue scope includes title-only, kind-zero and hidden live handles. */
    other = wind_create(NAME,0,0,200,140);
    CHECK(other > 0 && wind_open(other,20,30,200,140) == 1);
    control_target = native_windows[other-1]; control_at = control_count = 0;
    control_event(25,35,1,1); control_event(25,35,0,1);
    CHECK(evnt_timer(0) == 1 && control_at == control_count && !pipe_count);
    CHECK(wind_close(other) == 1);
    control_read_error = ERANGE;
    CHECK(evnt_timer(0) == 1 && !control_read_error && !pipe_count);
    CHECK(wind_delete(other) == 1);
    other = wind_create(0,0,0,200,140);
    CHECK(other > 0 && wind_open(other,20,30,200,140) == 1);
    control_target = native_windows[other-1]; control_at = control_count = 0;
    control_event(20,30,1,1); control_event(20,30,0,1);
    CHECK(appl_read(APR_NOWAIT,16,message) == 0 && control_at == control_count);
    CHECK(wind_close(other) == 1 && wind_delete(other) == 1);
    control_target = native_windows[id-1];
    /* Busy display preserves stream progress; overlays postpone all controls. */
    control_at = control_count = 0;
    control_event(25,35,1,1); control_event(25,35,0,1);
    display_error = EAGAIN; pipe_count = 16; before = control_at;
    CHECK(evnt_mesag(message) == 1 && control_at == before);
    display_error = 0;
    CHECK(aes_frames_suspend());
    CHECK(evnt_timer(0) == 1 && control_at == before && !(waited_mask & CUIS_READY_CONTROL));
    aes_frames_resume();
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_CLOSED);
    control_at = control_count = 0;
    control_event(25,35,1,1); control_event(25,35,0,1);
    frame_paint_failure = TRUE;
    CHECK(evnt_timer(0) == 1 && !pipe_count);
    frame_paint_failure = FALSE;
    control_at = control_count = 0; control_event(25,35,1,1);
    CHECK(evnt_timer(0) == 1 && control_selection);
    control_event(25,35,0,1); frame_paint_failure = TRUE;
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_CLOSED);
    frame_paint_failure = FALSE;
    control_at = control_count = 0;
    control_event(25,35,1,1); control_event(25,35,0,1);
    pipe_count = sizeof(pipe_data);
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48);
    CHECK(wind_close(id) == 1);
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48);
    CHECK(evnt_mesag(message) == 1 && evnt_mesag(message) == 1 && evnt_mesag(message) == 1);
    CHECK(message[0] == WM_CLOSED && message[4] == 20 && message[5] == 30 && message[6] == 200 && message[7] == 140);
    CHECK(wind_delete(id) == 1);
    control_target = 0; control_at = control_count = 0;
    return 0;
}

static int move_api_checks(void)
{
    short id, message[8], x,y,w,h;
    ULONG before;
    id = wind_create(NAME|MOVER,0,0,200,140);
    CHECK(id > 0 && wind_open(id,20,30,200,140) == 1);
    control_target = native_windows[id-1]; control_at = control_count = 0;
    before = move_calls;
    control_event(80,35,1,1); pipe_count = 16;
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && !pipe_count && control_at == 1 && move_calls == before);
    CHECK(appl_read(APR_NOWAIT,16,message) == 0 && control_at == 1 && move_calls == before);
    CHECK(evnt_timer(0) == 1 && move_calls == before);
    control_event(180,85,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_MOVED && message[4] == 120 && message[5] == 80);
    control_at = control_count = 0;
    state_values[2] = 0;
    control_event(80,35,1,1); control_event(180,85,0,1);
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && message[0] == WM_MOVED && message[4] == 120 && message[5] == 80);
    CHECK(move_calls == before+1); /* Polling completion never calls the blocking tracker. */
    state_values[2] = 2; control_at = control_count = 0;
    control_event(80,35,1,1); control_event(140,65,1,0); control_event(180,85,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_MOVED && message[3] == id);
    CHECK(message[4] == 120 && message[5] == 80 && message[6] == 200 && message[7] == 140);
    CHECK(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && x == 20 && y == 30 && w == 200 && h == 140);
    control_at = control_count = 0;
    control_event(80,35,1,1); control_event(-100,-100,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_MOVED && message[4] == 0 && message[5] == 19);
    control_event(80,35,1,1); control_event(32767,32767,0,1);
    CHECK(evnt_mesag(message) == 1 && message[4] == frame_screen_width-29 && message[5] == 9879);
    control_at = control_count = 0;
    control_event(80,35,3,3); control_event(180,85,0,3);
    CHECK(evnt_timer(0) == 1 && !pipe_count);
    /* A blocked move request retains the proposed, not current, geometry. */
    control_event(80,35,1,1); control_event(180,85,0,1);
    pipe_count = sizeof(pipe_data);
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48);
    CHECK(wind_set(id,WF_CURRXYWH,30,40,220,150) == 1);
    CHECK(appl_read(pid,16,message) == 1 && evnt_mesag(message) == 1 && evnt_mesag(message) == 1 && evnt_mesag(message) == 1);
    CHECK(message[0] == WM_MOVED && message[4] == 120 && message[5] == 80 && message[6] == 200 && message[7] == 140);
    CHECK(wind_close(id) == 1 && wind_delete(id) == 1);
    control_target = 0; control_at = control_count = 0;
    return 0;
}

static int size_api_checks(void)
{
    short id, message[8], x,y,w,h;
    ULONG before;
    id = wind_create(NAME|CLOSER|FULLER|MOVER|SIZER,0,0,200,140);
    CHECK(id > 0 && wind_open(id,20,30,200,140) == 1);
    control_target = native_windows[id-1]; control_at = control_count = 0;
    control_event(210,160,1,1); control_event(240,185,1,0); control_event(260,200,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_SIZED && message[3] == id);
    CHECK(message[4] == 20 && message[5] == 30 && message[6] == 241 && message[7] == 171);
    CHECK(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && x == 20 && y == 30 && w == 200 && h == 140);
    control_at = control_count = 0;
    control_event(210,160,1,1); control_event(-100,-100,0,1);
    CHECK(evnt_mesag(message) == 1 && message[6] == 76 && message[7] == 44);
    control_at = control_count = 0; before = size_calls;
    control_event(210,160,1,1); pipe_count = 16;
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && !pipe_count && size_calls == before);
    CHECK(appl_read(APR_NOWAIT,16,message) == 0 && size_calls == before);
    CHECK(evnt_timer(0) == 1 && size_calls == before);
    control_event(260,200,0,1);
    CHECK(evnt_mesag(message) == 1 && message[0] == WM_SIZED && message[6] == 241 && message[7] == 171);
    control_at = control_count = 0; state_values[2] = 0;
    control_event(210,160,1,1); control_event(260,200,0,1);
    CHECK(appl_read(APR_NOWAIT,16,message) == 1 && message[0] == WM_SIZED && size_calls == before+1);
    state_values[2] = 2; control_at = control_count = 0;
    control_event(210,160,3,3); control_event(260,200,0,3);
    CHECK(evnt_timer(0) == 1 && !pipe_count);
    control_event(210,160,1,1); control_event(260,200,0,1); pipe_count = sizeof(pipe_data);
    CHECK(appl_read(pid,16,message) == 1 && pipe_count == 48);
    CHECK(wind_set(id,WF_CURRXYWH,30,40,210,150) == 1);
    CHECK(appl_read(pid,16,message) == 1 && evnt_mesag(message) == 1 && evnt_mesag(message) == 1 && evnt_mesag(message) == 1);
    CHECK(message[0] == WM_SIZED && message[4] == 20 && message[5] == 30 && message[6] == 241 && message[7] == 171);
    CHECK(wind_close(id) == 1 && wind_delete(id) == 1);
    control_target = 0; control_at = control_count = 0;
    return 0;
}

static int control_timer_checks(void)
{
    short id;
    ULONG before;
    id = wind_create(0,0,0,100,100);
    CHECK(id > 0 && wind_open(id,20,30,100,100) == 1);
    control_target = native_windows[id-1]; control_at = control_count = 0;
    control_only_wakes = 3; menu_ticks = 0; before = wait_calls;
    CHECK(evnt_timer(20) == 1 && waited_interval == 0 && menu_ticks == 6 && wait_calls == before+3);
    control_only_wakes = 3; menu_ticks = 0x7ffffffeUL; before = wait_calls;
    CHECK(evnt_timer(20) == 1 && waited_interval == 0 && wait_calls == before+3);
    control_only_wakes = 0;
    display_error = EAGAIN;
    CHECK(aes_window_messages(handle,aes_global) == E_OK && aes_frames_wait_display() && !aes_controls_listening());
    release_display_on_wait = TRUE; display_release_ticks = 6; menu_ticks = 0; before = wait_calls;
    CHECK(evnt_timer(20) == 1 && waited_interval == 0 && wait_calls == before+2 && !display_error);
    CHECK(wind_close(id) == 1 && wind_delete(id) == 1);
    control_target = 0; control_at = control_count = 0;
    return 0;
}

static int display_api_checks(void)
{
    short id, other, x = 99, y, w, h;
    CHECK(wind_update(END_UPDATE) == 0);
    CHECK(wind_update(END_MCTRL) == 0 && wind_update(-1) == 0);
    CHECK(wind_update(BEG_MCTRL | NO_BLOCK) == 1 && !mouse_block && mouse_nesting == 1);
    CHECK(mt_wind_update(BEG_MCTRL, aes_global) == 1 && mouse_block == 1 && mouse_nesting == 2);
    CHECK(wind_update(END_MCTRL | NO_BLOCK) == 0 && wind_update(END_UPDATE | NO_BLOCK) == 0);
    CHECK(mouse_nesting == 2 && !display_nesting);
    CHECK(wind_update(END_MCTRL) == 1 && mouse_nesting == 1);
    mouse_error = EAGAIN;
    CHECK(wind_update(BEG_MCTRL | NO_BLOCK) == 0 && mouse_nesting == 1);
    CHECK(wind_update(END_MCTRL) == 0 && mouse_nesting == 1);
    mouse_error = 0;
    CHECK(mt_wind_update(END_MCTRL,aes_global) == 1 && !mouse_nesting);
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
    /* Another region consumer replaces the kernel snapshot. NEXT must
     * still use the original caller-owned rectangles without transport. */
    region_alloc_failure = TRUE; x = 99;
    CHECK(wind_get(id, WF_FIRSTXYWH, &x, &y, &w, &h) == 0 && x == 99);
    region_alloc_failure = FALSE;
    region_error = EIMBA;
    CHECK(wind_get(id, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && x == 10 && y == 1 && w == 9 && h == 18);
    CHECK(wind_get(other, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && x == 10);
    CHECK(wind_get(id, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    CHECK(wind_get(id, WF_NEXTXYWH, &x, &y, &w, &h) == 1 && !w && !h);
    region_error = 0;
    CHECK(wind_get(id, WF_FIRSTXYWH, &x, &y, &w, &h) == 1 && x == 1);
    CHECK(wind_delete(id) == 1 && wind_delete(other) == 1 && display_nesting == 1);
    CHECK(wind_update(END_UPDATE) == 1 && !display_nesting);
    display_error = EAGAIN;
    CHECK(wind_update(BEG_UPDATE | NO_BLOCK) == 0 && wind_create(0, 0, 0, 20, 20) == -1);
    display_error = 0; region_count = 0;
    return 0;
}
static int window_message_checks(void)
{
    short id, message[8], x, y, buttons, mods, key, clicks;
    ULONG before;
    id = wind_create(0, 0, 0, 100, 100);
    CHECK(id > 0 && wind_open(id, 10, 20, 30, 40) == 1);
    notice_native = native_windows[id - 1]; notice_id = id;
    notice_kind = CUIS_NOTICE_REDRAW; publish_failure = ENSMEM;
    CHECK(evnt_mesag(message) == 0 && notice_kind == CUIS_NOTICE_REDRAW && !pipe_count);
    publish_failure = 0; read_error = EIMBA;
    CHECK(evnt_mesag(message) == 0 && !notice_kind && pipe_count == 16);
    read_error = 0;
    CHECK(appl_read(APR_NOWAIT, 16, message) == 1 && message[0] == WM_REDRAW && !message[1] && !message[2]);
    CHECK(message[3] == id && message[4] == 11 && message[5] == 21 && message[6] == 28 && message[7] == 38);
    notice_on_wait = CUIS_NOTICE_ACTIVATE; before = wait_calls;
    CHECK(evnt_multi(MU_MESAG | MU_TIMER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, message, 0,
                     &x, &y, &buttons, &mods, &key, &clicks) == (MU_MESAG | MU_TIMER));
    CHECK(wait_calls == before + 1 && message[0] == WM_TOPPED && message[3] == id && !message[4]);
    notice_kind = CUIS_NOTICE_REDRAW;
    CHECK(evnt_multi(MU_TIMER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, 0,
                     &x, &y, &buttons, &mods, &key, &clicks) == MU_TIMER && notice_kind);
    __builtin_memset(pipe_data, 7, sizeof(pipe_data)); pipe_count = sizeof(pipe_data);
    CHECK(appl_read(APR_NOWAIT, 16, message) == 1 && notice_kind && pipe_count == 48);
    CHECK(appl_read(APR_NOWAIT, 16, message) == 1 && !notice_kind && pipe_count == 48);
    pipe_count = 0;
    CHECK(wind_set(id, WF_TOP, 0, 0, 0, 0) == 1 && wind_set(id, WF_BOTTOM, 0, 0, 0, 0) == 1);
    CHECK(wind_close(id) == 1 && wind_set(id, WF_TOP, 0, 0, 0, 0) == 0 && wind_delete(id) == 1);
    return 0;
}
static int metadata_checks(void)
{
    short a = 91, b = 92, c = 93, d = 94;
    stack_values[0] = 1; stack_values[1] = 32767; stack_values[2] = 2;
    CHECK(wind_get(0, WF_TOP, &a, &b, &c, &d) == 1 && a == 1 && b == 32767 && c == 2 && !d);
    a = 91; b = 92; c = 93; d = 94;
    stack_values[1] = 32768;
    CHECK(wind_get(0, WF_TOP, &a, &b, &c, &d) == 0 && a == 91 && b == 92 && c == 93 && d == 94);
    stack_values[1] = 7; stack_values[0] = 65537;
    CHECK(wind_get(0, WF_BOTTOM, &a, &b, &c, &d) == 0 && a == 91);
    stack_values[0] = -1; stack_values[1] = 1;
    CHECK(wind_get(1, WF_OWNER, &a, &b, &c, &d) == 0 && a == 91);
    stack_values[0] = 7; stack_values[1] = 2;
    CHECK(wind_get(1, WF_OWNER, &a, &b, &c, &d) == 0 && a == 91);
    stack_values[1] = 1; stack_values[3] = 32768;
    CHECK(wind_get(1, WF_OWNER, &a, &b, &c, &d) == 0 && a == 91);
    stack_values[3] = 3;
    CHECK(wind_get(1, WF_OWNER, &a, &b, &c, &d) == 1 && a == 7 && b == 1 && c == 2 && d == 3);
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
    CHECK(appl_getinfo(AES_PROCESS, &a, &b, &c, &d) == 1 && a == 0 && b == 1 && c == 0 && d == 1);
    CHECK(appl_getinfo(AES_OBJECT, &a, &b, &c, &d) == 1 && a == 1 && b == 1 && c == 0 && d == 0);
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
    CHECK(mt_evnt_mesag(result, other) == 1 && pipe_block == 0);
    CHECK(!__builtin_memcmp(result, message, sizeof(message)));
    CHECK(appl_write(7, 16, message) == 1 && appl_read(APR_NOWAIT, 16, result) == 1 && pipe_block == 0);
    failure = EACCDN;
    CHECK(appl_write(7, 16, message) == 0 && evnt_mesag(result) == 0);
    failure = 0;
    CHECK(event_checks() == 0 && input_api_checks() == 0);
    CHECK(gem_window_checks(other) == 0 && window_failures() == 0 && frame_failures() == 0 && frame_event_checks() == 0 && control_api_checks() == 0 && control_timer_checks() == 0 && move_api_checks() == 0 && size_api_checks() == 0 && display_api_checks() == 0 && window_message_checks() == 0 && metadata_checks() == 0);
    CHECK(mt_appl_getinfo(-1, NULL, NULL, NULL, NULL, other) == 0);
    other[2] = 8; CHECK(mt_appl_exit(other) == 0 && live); other[2] = 7;
    held = wind_create(0, 0, 0, 10, 10); CHECK(held > 0);
    resource_resets = 0;
    close_result = EACCDN;
    CHECK(appl_exit() == 0 && live && gl_apid == 7 && !resource_resets);
    CHECK(wind_get(held, WF_KIND, NULL, NULL, NULL, NULL) == 1);
    close_result = 0;
    CHECK(mt_appl_exit(other) == 1 && !live && closes == 1 && other[2] == -1 && resource_resets == 1);
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
