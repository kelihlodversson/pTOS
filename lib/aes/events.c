/* Standard GEM application input, message and timer events. */
#include "gem.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "menubar.h"

static short coordinate(LONG value)
{
    return value > 32767 ? 32767 : value < -32768 ? -32768 : (short)value;
}

static LONG snapshot(LONG session, LONG state[4])
{
    return cuis_call6(CUIS_INPUT_STATE, session, (uintptr_t)&state[0], (uintptr_t)&state[1],
                      (uintptr_t)&state[2], (uintptr_t)&state[3], 0);
}

static void output_state(const LONG state[4], short *mx, short *my, short *buttons, short *modifiers)
{
    if (mx) *mx = coordinate(state[0]);
    if (my) *my = coordinate(state[1]);
    if (buttons) *buttons = (short)(state[2] & 3);
    if (modifiers) *modifiers = (short)(state[3] & 15);
}

short mt_graf_mkstate(short *mx, short *my, short *buttons, short *modifiers, short *global)
{
    LONG state[4], session = aes_application_session(global);
    if (session < 0 || snapshot(session, state) != E_OK) return 0;
    output_state(state, mx, my, buttons, modifiers);
    return 1;
}
short graf_mkstate(short *mx, short *my, short *buttons, short *modifiers)
{
    return mt_graf_mkstate(mx, my, buttons, modifiers, aes_global);
}

static short event_once(short events, short clicks, short button_mask, short button_state,
                    short m1_leave, short m1_x, short m1_y, short m1_w, short m1_h,
                    short m2_leave, short m2_x, short m2_y, short m2_w, short m2_h,
                    short message[], unsigned long interval, short *mx, short *my,
                    short *buttons, short *modifiers, short *key, short *click_count, short *global, BOOL clocked, LONG start, ULONG duration)
{
    LONG session, ready, result = 0, state[4], input[5];
    ULONG mask = 0, wait_mask, elapsed, ticks;
    LONG now;
    BOOL frame_work = aes_frames_registered();
    if (!events || (events & ~(MU_KEYBD | MU_BUTTON | MU_M1 | MU_M2 | MU_MESAG | MU_TIMER)) ||
        ((events & MU_MESAG) && !message) || ((events & MU_TIMER) && interval > 0xffffffffUL) ||
        ((events & MU_BUTTON) && ((UWORD)clicks & 0xfe00)) ||
        ((events & MU_M1) && (m1_leave < 0 || m1_leave > 1)) ||
        ((events & MU_M2) && (m2_leave < 0 || m2_leave > 1))) return 0;
    session = aes_application_session(global);
    if (session < 0) return 0;
    if (events & MU_KEYBD) mask |= CUIS_READY_KEY;
    if (events & MU_BUTTON) {
        if (cuis_call6(CUIS_INPUT_BUTTON, session, (UWORD)button_mask, (UWORD)button_state,
                       clicks & 255, ((UWORD)clicks >> 8) & 1, 0) < 0) return 0;
        mask |= CUIS_READY_BUTTON;
    }
    if (events & MU_M1) {
        if (cuis_call6(CUIS_INPUT_RECT, session, m1_leave * 2, (QUAD)m1_x, (QUAD)m1_y,
                       (QUAD)m1_w, (QUAD)m1_h) < 0) return 0;
        mask |= CUIS_READY_RECT1;
    }
    if (events & MU_M2) {
        if (cuis_call6(CUIS_INPUT_RECT, session, 1 + m2_leave * 2, (QUAD)m2_x, (QUAD)m2_y,
                       (QUAD)m2_w, (QUAD)m2_h) < 0) return 0;
        mask |= CUIS_READY_RECT2;
    }
    if (((events & MU_MESAG) || frame_work) && aes_window_messages(session, global) < 0) return 0;
    if (events & MU_MESAG) mask |= CUIS_READY_STREAM | CUIS_READY_NOTICE;
    if (events & MU_TIMER) mask |= CUIS_READY_TIMER;
    wait_mask = mask | (aes_menu_listening() ? CUIS_READY_SURFACE : 0);
    /* A multi-click owns per-call accumulation until completion. Service
     * frame work afterward rather than interrupting that unfinished count. */
    if (aes_frames_listening() && (!(events & MU_BUTTON) || (clicks & 255) <= 1))
        wait_mask |= CUIS_READY_NOTICE;
    if (aes_controls_listening()) wait_mask |= CUIS_READY_CONTROL;
    if (aes_frames_suspended()) wait_mask &= ~CUIS_READY_NOTICE;
    else if (aes_frames_wait_display()) {
        wait_mask &= ~(CUIS_READY_NOTICE|CUIS_READY_CONTROL); wait_mask |= CUIS_READY_DISPLAY;
    }
    if (clocked) {
        now = cuis_call6(CUIS_CLOCK,session,0,0,0,0,0);
        if (now < 0) return 0;
        elapsed = ((ULONG)now-(ULONG)start) & 0x7fffffffUL;
        ticks = elapsed >= duration ? 0 : duration-elapsed;
        interval = ticks > 1 ? (ticks-1)*5 : ticks ? 1 : 0;
    }
    ready = cuis_call6(CUIS_SESSION_WAIT, session, 16, wait_mask, interval, 0, 0);
    if (ready <= 0 || ((ULONG)ready & ~wait_mask)) return 0;
    if ((ready & (CUIS_READY_NOTICE|CUIS_READY_DISPLAY|CUIS_READY_CONTROL)) && aes_window_messages(session, global) < 0) return 0;
    if (mask & CUIS_READY_INPUT) {
        if (cuis_call6(CUIS_INPUT_RESULT, session, (uintptr_t)&input[0], (uintptr_t)&input[1],
                       (uintptr_t)&input[2], (uintptr_t)&input[3], (uintptr_t)&input[4]) != E_OK) return 0;
        state[0] = input[2]; state[1] = input[3];
        state[2] = (ULONG)input[4] & 0xffff; state[3] = (ULONG)input[4] >> 16;
    } else if (snapshot(session, state) != E_OK) return 0;
    if (frame_work && !(ready & CUIS_READY_NOTICE) && aes_window_messages(session,global) < 0) return 0;
    if ((ready & CUIS_READY_NOTICE) && aes_frames_wait_display()) {
        ready &= ~CUIS_READY_NOTICE;
        if (!ready) return -1;
    }
    if (ready & (CUIS_READY_DISPLAY|CUIS_READY_CONTROL)) {
        ready &= ~(CUIS_READY_DISPLAY|CUIS_READY_CONTROL);
        if (!ready) return -1;
    }
    if ((ready & CUIS_READY_NOTICE) && !(mask & CUIS_READY_NOTICE)) {
        ready &= ~CUIS_READY_NOTICE;
        if (!ready) return -1;
    }
    if (ready & CUIS_READY_SURFACE) {
        if (aes_menu_dispatch(global) < 0) return 0;
        ready &= ~CUIS_READY_SURFACE;
        if (!ready) return -1; /* Re-arm user predicates after internal tracking. */
    }
    if (ready & (CUIS_READY_STREAM | CUIS_READY_NOTICE)) {
        if (cuis_call6(CUIS_STREAM_READ, session, (uintptr_t)message, 16, 0, 0, 0) != 16) return 0;
        result |= MU_MESAG;
    }
    if (ready & CUIS_READY_TIMER) result |= MU_TIMER;
    output_state(state, mx, my, buttons, modifiers);
    if (ready & CUIS_READY_KEY) result |= MU_KEYBD;
    if (ready & CUIS_READY_BUTTON) result |= MU_BUTTON;
    if (ready & CUIS_READY_RECT1) result |= MU_M1;
    if (ready & CUIS_READY_RECT2) result |= MU_M2;
    if (key) *key = ready & CUIS_READY_KEY ? (short)(((ULONG)input[0] >> 8 & 0xff00) | (input[0] & 255)) : 0;
    if (click_count) *click_count = ready & CUIS_READY_BUTTON ? (short)input[1] : 0;
    return (short)result;
}
short mt_evnt_multi(short events, short clicks, short button_mask, short button_state,
                    short m1_leave, short m1_x, short m1_y, short m1_w, short m1_h,
                    short m2_leave, short m2_x, short m2_y, short m2_w, short m2_h,
                    short message[], unsigned long interval, short *mx, short *my,
                    short *buttons, short *modifiers, short *key, short *click_count, short *global)
{
    LONG start = 0, session;
    ULONG duration;
    short result;
    BOOL clocked = (events & MU_TIMER) && (aes_menu_registered() || aes_frames_registered());
    if ((events & MU_TIMER) && interval > 0xffffffffUL) return 0;
    session = aes_application_session(global);
    duration = interval/5 + (interval%5 != 0) + (interval != 0);
    if (clocked) {
        start = cuis_call6(CUIS_CLOCK,session,0,0,0,0,0);
        if (start < 0) return 0;
    }
    for (;;) {
        if (aes_menu_dispatch(global) < 0) return 0;
        result = event_once(events,clicks,button_mask,button_state,m1_leave,m1_x,m1_y,m1_w,m1_h,
                            m2_leave,m2_x,m2_y,m2_w,m2_h,message,interval,mx,my,buttons,modifiers,key,click_count,global,clocked,start,duration);
        if (result >= 0) return result;
    }
}

short evnt_multi(short events, short clicks, short button_mask, short button_state,
                 short m1_leave, short m1_x, short m1_y, short m1_w, short m1_h,
                 short m2_leave, short m2_x, short m2_y, short m2_w, short m2_h,
                 short message[], unsigned long interval, short *mx, short *my,
                 short *buttons, short *modifiers, short *key, short *click_count)
{
    return mt_evnt_multi(events, clicks, button_mask, button_state,
                         m1_leave, m1_x, m1_y, m1_w, m1_h, m2_leave, m2_x, m2_y, m2_w, m2_h,
                         message, interval, mx, my, buttons, modifiers, key, click_count, aes_global);
}

short mt_evnt_keybd(short *global)
{
    short key = 0;
    mt_evnt_multi(MU_KEYBD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  NULL, 0, NULL, NULL, NULL, NULL, &key, NULL, global);
    return key;
}
short evnt_keybd(void) { return mt_evnt_keybd(aes_global); }

short mt_evnt_button(short clicks, short mask, short state, short *mx, short *my,
                     short *buttons, short *modifiers, short *global)
{
    short count = 0;
    mt_evnt_multi(MU_BUTTON, clicks, mask, state, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  NULL, 0, mx, my, buttons, modifiers, NULL, &count, global);
    return count;
}
short evnt_button(short clicks, short mask, short state, short *mx, short *my,
                  short *buttons, short *modifiers)
{
    return mt_evnt_button(clicks, mask, state, mx, my, buttons, modifiers, aes_global);
}
short mt_evnt_mouse(short leave, short x, short y, short width, short height,
                    short *mx, short *my, short *buttons, short *modifiers, short *global)
{
    return mt_evnt_multi(MU_M1, 0, 0, 0, leave, x, y, width, height, 0, 0, 0, 0, 0,
                         NULL, 0, mx, my, buttons, modifiers, NULL, NULL, global) == MU_M1;
}
short evnt_mouse(short leave, short x, short y, short width, short height,
                 short *mx, short *my, short *buttons, short *modifiers)
{
    return mt_evnt_mouse(leave, x, y, width, height, mx, my, buttons, modifiers, aes_global);
}
short mt_evnt_dclick(short rate, short set, short *global)
{
    static const ULONG intervals[5] = { 450, 330, 275, 220, 165 };
    LONG session = aes_application_session(global), value;
    short i;
    if (session < 0 || set < 0 || set > 1 || (set && (rate < 0 || rate > 4))) return 0;
    value = cuis_call6(CUIS_INPUT_INTERVAL, session, set ? intervals[rate] : 0, set, 0, 0, 0);
    for (i = 0; i < 5; i++) if (value == (LONG)intervals[i]) return i;
    return 0;
}
short evnt_dclick(short rate, short set) { return mt_evnt_dclick(rate, set, aes_global); }
