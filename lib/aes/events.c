/* Standard GEM message/timer combinations; input masks migrate separately. */
#include "gem.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"

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

short mt_evnt_multi(short events, short clicks, short button_mask, short button_state,
                    short m1_leave, short m1_x, short m1_y, short m1_w, short m1_h,
                    short m2_leave, short m2_x, short m2_y, short m2_w, short m2_h,
                    short message[], unsigned long interval, short *mx, short *my,
                    short *buttons, short *modifiers, short *key, short *click_count, short *global)
{
    LONG session, ready, result = 0, state[4];
    ULONG mask = 0;
    (void)clicks; (void)button_mask; (void)button_state;
    (void)m1_leave; (void)m1_x; (void)m1_y; (void)m1_w; (void)m1_h;
    (void)m2_leave; (void)m2_x; (void)m2_y; (void)m2_w; (void)m2_h;
    /* Fail before waiting or consuming bytes, rather than silently ignoring
     * keyboard/button/rectangle requests whose semantics have not migrated. */
    if (!events || (events & ~(MU_MESAG | MU_TIMER)) ||
        ((events & MU_MESAG) && !message) || ((events & MU_TIMER) && interval > 0xffffffffUL)) return 0;
    session = aes_application_session(global);
    if (session < 0) return 0;
    if (events & MU_MESAG) mask |= CUIS_READY_STREAM;
    if (events & MU_TIMER) mask |= CUIS_READY_TIMER;
    ready = cuis_call6(CUIS_SESSION_WAIT, session, 16, mask, interval, 0, 0);
    if (ready <= 0 || ((ULONG)ready & ~mask) || snapshot(session, state) != E_OK) return 0;
    if (ready & CUIS_READY_STREAM) {
        if (cuis_call6(CUIS_STREAM_READ, session, (uintptr_t)message, 16, 0, 0, 0) != 16) return 0;
        result |= MU_MESAG;
    }
    if (ready & CUIS_READY_TIMER) result |= MU_TIMER;
    output_state(state, mx, my, buttons, modifiers);
    if (key) *key = 0;
    if (click_count) *click_count = 0;
    return (short)result;
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
