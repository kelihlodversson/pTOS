/* Loaded ring-3 windows: ownership, copied buffers and real native routing. */
#include <mint/mintbind.h>
#include <stdio.h>
#include <string.h>
#include "aes_client.h"
#include "aes_service.h"
#include "gemerror.h"
#include "vdipb.h"

int accessory_window_checks(const AES_APP_INFO *info, WORD role, WORD vdi_handle);
int accessory_window_redraw(WORD handle, WORD vdi_handle);
int accessory_window_reopen(WORD role, WORD vdi_handle);

static volatile LONG revoked, reregistered;
static AES_APP_INFO callback_info;

static void revoke_window(WORD status)
{
    if (status == 626) {
        revoked = aes_appl_exit();
        reregistered = aes_appl_init("ACCTWO", &callback_info);
    }
}

#define CHECK(c) do { if (!(c)) { char failure[80]; \
    snprintf(failure, sizeof(failure), "accessory window FAIL line %d: ", __LINE__); \
    (void)Cconws(failure); (void)Cconws(#c "\r\n"); return 99; } } while (0)

static const UWORD kind = AES_WINDOW_NAME_GADGET | AES_WINDOW_CLOSER |
    AES_WINDOW_MOVER | AES_WINDOW_INFO_GADGET | AES_WINDOW_SIZER |
    AES_WINDOW_HSLIDE_GADGET | AES_WINDOW_VSLIDE_GADGET;
static const AES_WINDOW_INFO immutable_info = { 0 };
static const GRECT immutable_rect = { 0 };

int accessory_window_redraw(WORD handle, WORD vdi_handle)
{
    AES_WINDOW_INFO info;
    GRECT visible;
    VDICONTROL c = { 0 };
    WORD input[24] = { 0 }, output[2], points[4] = { 0 }, pointout[2], y;
    VDIPB pb = { &c, input, points, output, pointout };
    LONG result;

    do {
        result = aes_wind_update(AES_BEG_UPDATE);
        if (result == EAGAIN)
            CHECK(aes_appl_yield() == E_OK);
    } while (result == EAGAIN);
    CHECK(result == E_OK);
    do {
        result = aes_wind_get(handle, &info);
        if (result == EAGAIN)
            CHECK(aes_appl_yield() == E_OK);
    } while (result == EAGAIN);
    if (result == EIHNDL || result == EACCDN) {
        CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
        return 0;
    }
    CHECK(result == E_OK);
    c.handle = vdi_handle;
    CHECK(aes_wind_rect(handle, 0, &visible) == E_OK);
    while (visible.g_w && visible.g_h) {
        points[0] = visible.g_x; points[1] = visible.g_y;
        points[2] = visible.g_x + visible.g_w - 1;
        points[3] = visible.g_y + visible.g_h - 1;
        c.code = 129; c.nptsin = 2; c.nintin = 1; input[0] = 1;
        CHECK(vdi(&pb) == E_OK);
        /* Use the existing narrow VDI drawing ABI: color, clip and lines. */
        c.code = 17; c.nptsin = 0; input[0] = 0;
        CHECK(vdi(&pb) == E_OK);
        for (y = visible.g_y; y < visible.g_y + visible.g_h; y++) {
            points[0] = visible.g_x; points[1] = points[3] = y;
            points[2] = visible.g_x + visible.g_w - 1;
            c.code = 6; c.nintin = 0; c.nptsin = 2;
            CHECK(vdi(&pb) == E_OK);
        }
        c.code = 17; c.nintin = 1; c.nptsin = 0; input[0] = 1;
        CHECK(vdi(&pb) == E_OK);
        points[0] = info.work.g_x + 8; points[1] = info.work.g_y + 8;
        points[2] = info.work.g_x + info.work.g_w - 9;
        points[3] = info.work.g_y + info.work.g_h - 9;
        c.code = 6; c.nintin = 0; c.nptsin = 2;
        CHECK(vdi(&pb) == E_OK);
        points[1] = info.work.g_y + info.work.g_h - 9;
        points[3] = info.work.g_y + 8;
        CHECK(vdi(&pb) == E_OK);
        CHECK(aes_wind_rect(handle, 1, &visible) == E_OK);
    }
    points[0] = points[1] = 0; points[2] = points[3] = 1;
    c.code = 129; c.nptsin = 2; c.nintin = 1; input[0] = 0;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
    return 0;
}

static LONG raw_window(UQUAD request, UQUAD result, UQUAD extra)
{
    register UQUAD rax __asm__("rax") = ((UQUAD)AES_SERVICE_CLASS << 32) | AES_SERVICE_WINDOW;
    register UQUAD rdi __asm__("rdi") = request;
    register UQUAD rsi __asm__("rsi") = result;
    register UQUAD rdx __asm__("rdx") = extra;
    register UQUAD r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)rax;
}

static int copied_text(WORD handle)
{
    LONG memory = Malloc(4096);
    char *text = (char *)(uintptr_t)(ULONG)memory;

    CHECK(memory > 0);
    strcpy(text, "Typed x32 window");
    CHECK(aes_wind_set_text(handle, AES_WINDOW_NAME, text) == E_OK);
    strcpy(text, "Copied window information");
    CHECK(aes_wind_set_text(handle, AES_WINDOW_INFO_LINE, text) == E_OK);
    memset(text, 'X', 4096);
    CHECK(Mfree(text) == E_OK);
    return 0;
}

int accessory_window_reopen(WORD role, WORD vdi_handle)
{
    GRECT rectangle = { 48, 60, 356, 240 };
    LONG handle;

    if (role == 4) {
        rectangle.g_x = 440;
        rectangle.g_y = 140;
    }
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && copied_text((WORD)handle) == 0);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
    CHECK(accessory_window_redraw((WORD)handle, vdi_handle) == 0);
    (void)Cconws("x32 window reopened on desktop: PASS\r\n");
    return 0;
}

int accessory_window_checks(const AES_APP_INFO *info, WORD role, WORD vdi_handle)
{
    static const char *names[3] = { "ACCONE", "ACCFAULT", "ACCWAIT" };
    AES_MESSAGE message = { 626, 0, 0, { 0 } }, received;
    AES_EVENT event;
    AES_EVENT_REQUEST wait = { AES_EVENT_MESSAGE | AES_EVENT_TIMER, 0, 0, 0, 0, 0,
                               { 0 }, { 0 }, 3000 };
    AES_INPUT_EVENT input_event;
    AES_WINDOW_INFO state, desktop;
    AES_APP_INFO renewed;
    AES_WINDOW_REQUEST raw = { AES_WIN_GET, 0 };
    VDICONTROL control = { 0 };
    VDIPB pb = { &control, NULL, NULL, NULL, NULL };
    VDI_BUTTON_CALLBACK *old_button;
    GRECT rectangle = { 40, 60, 340, 240 }, calculated, work, visible;
    union { AES_WINDOW_REQUEST request; AES_WINDOW_INFO result; } alias;
    char line[100], too_long[AES_WINDOW_TEXT_MAX + 1];
    LONG handle, peer, target, extra, memory;
    WORD exhausted[32];
    UBYTE *edge;
    WORD i, count;

    CHECK(info->features & AES_FEATURE_WINDOW);
    if (role != 2 && role != 4) {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
        CHECK(event.message.type == 626);
    }
    if (role != 2) {
        rectangle.g_x += role * 100;
        rectangle.g_y += role * 20;
        handle = aes_wind_create(kind, &rectangle);
        CHECK(handle > 0 && copied_text((WORD)handle) == 0);
        CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
        if (role == 4) {
            peer = aes_appl_find("ACCTWO");
            CHECK(peer > 0);
            message.type = 627; message.data[0] = handle;
            CHECK(aes_appl_write((WORD)peer, &message) == E_OK);
        } else {
            /* Reclaim both a live window and a created-but-closed window
             * when this task returns normally or faults. */
            extra = aes_wind_create(kind, &rectangle);
            CHECK(extra > 0 && copied_text((WORD)extra) == 0);
        }
        return 0;
    }
    for (i = 0; i < 3; i++) {
        target = aes_appl_find(names[i]);
        CHECK(target > 0);
        CHECK(aes_appl_write((WORD)target, &message) == E_OK);
    }
    do {
        CHECK(aes_evnt_multi(&wait, &input_event) == AES_EVENT_MESSAGE);
    } while (input_event.message.type == 20);
    CHECK(input_event.message.type == 627);
    peer = input_event.message.data[0];
    CHECK(peer > 0);
    CHECK(aes_wind_get((WORD)peer, &state) == EACCDN);
    CHECK(aes_wind_open((WORD)peer, &rectangle) == EACCDN);
    CHECK(aes_wind_close((WORD)peer) == EACCDN);
    CHECK(aes_wind_delete((WORD)peer) == EACCDN);
    CHECK(aes_wind_top((WORD)peer) == EACCDN);
    CHECK(aes_wind_set_rect((WORD)peer, &rectangle) == EACCDN);
    CHECK(aes_wind_set_text((WORD)peer, AES_WINDOW_NAME, "stolen") == EACCDN);
    CHECK(aes_wind_slider((WORD)peer, AES_WINDOW_HSLIDE, 500) == EACCDN);
    CHECK(aes_wind_rect((WORD)peer, 0, &visible) == EACCDN);
    CHECK(aes_wind_get(0, &desktop) == E_OK && !desktop.owner && !desktop.reserved);
    CHECK(desktop.work.g_w > 0 && desktop.work.g_h > 0);
    CHECK(aes_wind_delete(0) == EACCDN);
    CHECK(aes_wind_get(-1, &state) == EIHNDL);
    CHECK(aes_wind_get(32767, &state) == EIHNDL);
    CHECK(aes_wind_create(0x8000, &rectangle) == EINVAL);
    CHECK(aes_wind_create(kind, (const GRECT *)1) == EIMBA);
    CHECK(aes_wind_get(0, (AES_WINDOW_INFO *)&immutable_info) == EIMBA);
    CHECK(raw_window(0x100000000ULL + (uintptr_t)&raw, (uintptr_t)&state, 0) == EIMBA);
    CHECK(raw_window((uintptr_t)&raw, 0xffff800000000000ULL, 0) == EIMBA);
    CHECK(raw_window((uintptr_t)&raw, (uintptr_t)&state, 1) == EINVAL);
    raw.reserved = 1;
    CHECK(raw_window((uintptr_t)&raw, (uintptr_t)&state, 0) == EINVAL);
    raw.reserved = 0; raw.selector = 1;
    CHECK(raw_window((uintptr_t)&raw, (uintptr_t)&state, 0) == EINVAL);
    raw.selector = 0; alias.request = raw;
    CHECK(raw_window((uintptr_t)&alias, (uintptr_t)&alias, 0) == E_OK);
    CHECK(!alias.result.handle && !alias.result.owner);
    memory = Malloc(4096);
    CHECK(memory > 0);
    edge = (UBYTE *)(uintptr_t)(ULONG)memory;
    CHECK(aes_wind_create(kind, (const GRECT *)(edge + 4092)) == EIMBA);
    CHECK(aes_wind_get(0, (AES_WINDOW_INFO *)(edge + 4092)) == EIMBA);
    CHECK(Mfree(edge) == E_OK);
    CHECK(aes_wind_calc(AES_WINDOW_WORK, kind, &rectangle, &work) == E_OK);
    CHECK(aes_wind_calc(AES_WINDOW_BORDER, kind, &work, &calculated) == E_OK);
    CHECK(rc_equal(&rectangle, &calculated));
    CHECK(aes_wind_calc(2, kind, &rectangle, &work) == EINVAL);
    calculated = rectangle; calculated.g_x = 32760;
    CHECK(aes_wind_create(kind, &calculated) == EINVAL);
    calculated = rectangle; calculated.g_x = -2; calculated.g_w = 32767;
    CHECK(aes_wind_create(kind, &calculated) == EINVAL);
    calculated = work; calculated.g_x = calculated.g_y = 0;
    calculated.g_w = 32766 - (rectangle.g_w - work.g_w);
    CHECK(aes_wind_calc(AES_WINDOW_BORDER, kind, &calculated, &visible) == EINVAL);
    calculated = rectangle; calculated.g_w = 1;
    CHECK(aes_wind_create(kind, &calculated) == EINVAL);
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && handle != peer);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK);
    CHECK(state.owner == info->id && state.kind == kind && !state.flags && !state.reserved);
    CHECK(rc_equal(&rectangle, &state.full));
    CHECK(!state.work.g_w && !state.work.g_h);
    CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK && !visible.g_w && !visible.g_h);
    for (count = 0; count < 32; count++) {
        extra = aes_wind_create(kind, &rectangle);
        if (extra == ENSMEM)
            break;
        CHECK(extra > 0);
        exhausted[count] = extra;
    }
    CHECK(count > 0 && count < 32);
    for (i = 0; i < count; i++)
        CHECK(aes_wind_delete(exhausted[i]) == E_OK);
    CHECK(copied_text((WORD)handle) == 0);
    memset(too_long, 'X', sizeof(too_long));
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, too_long) == EINVAL);
    too_long[AES_WINDOW_TEXT_MAX] = 0;
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, too_long) == E_OK);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "") == E_OK);
    CHECK(copied_text((WORD)handle) == 0);
    CHECK(aes_wind_set_text((WORD)handle, 2, "bad") == EINVAL);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, (const char *)1) == EIMBA);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == EINVAL);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK && state.flags == AES_WINDOW_OPEN);
    CHECK(rc_equal(&rectangle, &state.current) && rc_equal(&work, &state.work));
    calculated = rectangle; calculated.g_x = -32000;
    CHECK(aes_wind_set_rect((WORD)handle, &calculated) == EINVAL);
    /* Two individually valid off-screen rectangles can have an unrepresentable
     * union. Include a closed window's old bounds in native move validation. */
    extra = aes_wind_create(0, &rectangle);
    CHECK(extra > 0);
    calculated = rectangle; calculated.g_x = -30000;
    CHECK(aes_wind_set_rect((WORD)extra, &calculated) == E_OK);
    calculated.g_x = 32000;
    CHECK(aes_wind_set_rect((WORD)handle, &calculated) == EINVAL);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK && rc_equal(&rectangle, &state.current));
    CHECK(aes_wind_delete((WORD)extra) == E_OK);
    CHECK(aes_wind_find(state.work.g_x + 2, state.work.g_y + 2) == handle);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_HSLIDE, 321) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_VSLIDE, 765) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_HSLSIZ, 400) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_VSLSIZ, -1) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, 4, 500) == EINVAL);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_HSLIDE, -1) == EINVAL);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK);
    CHECK(state.hslide == 321 && state.vslide == 765 && state.hslsiz == 400 && state.vslsiz == -1);
    CHECK(aes_wind_top((WORD)handle) == E_OK);
    CHECK(aes_wind_rect((WORD)handle, 0, (GRECT *)&immutable_rect) == EIMBA);
    CHECK(aes_wind_rect((WORD)handle, 2, &visible) == EINVAL);
    CHECK(aes_wind_rect((WORD)handle, 0, &visible) == E_OK);
    count = 0;
    while (visible.g_w && visible.g_h) {
        CHECK(++count < 128);
        calculated = visible;
        CHECK(rc_intersect(&state.work, &calculated) && rc_equal(&calculated, &visible));
        CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK);
    }
    CHECK(count > 0);
    /* Native rectangle nodes are recycled by window changes. NEXT must
     * rescan safely rather than follow a retained pointer into that pool. */
    CHECK(aes_wind_rect((WORD)handle, 0, &visible) == E_OK && visible.g_w > 0);
    CHECK(aes_wind_set_rect((WORD)handle, &rectangle) == E_OK);
    CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK && visible.g_w > 0);
    while (aes_appl_read(&received) == E_OK)
        CHECK(received.type == 20);
    /* Full opaque-message queue: self redraws must coalesce, never suspend
     * the window syscall waiting for this same caller to drain its queue. */
    message.type = 630;
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        message.data[0] = i;
        CHECK(aes_appl_write(info->id, &message) == E_OK);
    }
    for (i = 0; i < 4; i++) {
        rectangle.g_x += 2;
        rectangle.g_w += 4;
        CHECK(aes_wind_set_rect((WORD)handle, &rectangle) == E_OK);
    }
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        CHECK(aes_appl_read(&received) == E_OK);
        CHECK(received.type == 630 && received.data[0] == i);
    }
    CHECK(aes_appl_read(&received) == E_OK);
    CHECK(received.type == 20 && !received.sender && received.data[0] == handle);
    CHECK(received.data[3] > 0 && received.data[4] > 0);
    while (aes_appl_read(&received) == E_OK)
        CHECK(received.type == 20);
    wait.mask = AES_EVENT_KEYBOARD | AES_EVENT_MESSAGE | AES_EVENT_TIMER;
    (void)Cconws("x32 window key: READY\r\n");
    do {
        CHECK(aes_evnt_multi(&wait, &input_event) > 0);
        CHECK(!(input_event.events & AES_EVENT_TIMER));
    } while (!(input_event.events & AES_EVENT_KEYBOARD));
    CHECK(input_event.key == 0x3062);
    wait.mask = AES_EVENT_TIMER; wait.timer_ms = 0;
    CHECK(aes_evnt_multi(&wait, &input_event) == AES_EVENT_TIMER);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK);
    snprintf(line, sizeof(line), "x32 window close: READY %d %d %d %d\r\n",
             rectangle.g_x + 8, rectangle.g_y + 8, input_event.mouse_x, input_event.mouse_y);
    (void)Cconws(line);
    wait.mask = AES_EVENT_MESSAGE | AES_EVENT_TIMER; wait.timer_ms = 3000;
    do {
        CHECK(aes_evnt_multi(&wait, &input_event) == AES_EVENT_MESSAGE);
    } while (input_event.message.type == 20);
    CHECK(input_event.message.type == 22 && !input_event.message.sender &&
          input_event.message.data[0] == handle);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK && state.flags == AES_WINDOW_OPEN);
    CHECK(aes_wind_close((WORD)handle) == E_OK);
    CHECK(aes_wind_close((WORD)handle) == EINVAL);
    CHECK(aes_wind_top((WORD)handle) == EINVAL);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Recycle this window") == E_OK);
    CHECK(aes_wind_delete((WORD)handle) == E_OK);
    do {
        CHECK(aes_appl_read(&received) == E_OK);
    } while (received.type == 20);
    CHECK(received.type == 635 && received.data[0] == EAGAIN);
    CHECK(received.sender == aes_appl_find("ACCWAIT"));
    CHECK(aes_wind_get((WORD)handle, &state) == EIHNDL);
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && aes_wind_open((WORD)handle, &rectangle) == E_OK);
    CHECK(aes_appl_exit() == E_OK);
    CHECK(aes_appl_init("ACCTWO", &renewed) == E_OK && renewed.id == info->id);
    CHECK(aes_wind_get((WORD)handle, &state) == EIHNDL);
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && aes_wind_open((WORD)handle, &rectangle) == E_OK);
    extra = aes_wind_create(kind, &rectangle);
    CHECK(extra > 0);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Revoke this window") == E_OK);
    control.code = 125; control.handle = vdi_handle;
    control.args.button.new_handler = revoke_window;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    memset(&state, 0x7d, sizeof(state));
    revoked = reregistered = EINVAL;
    CHECK(aes_wind_get((WORD)handle, &state) == EACCDN);
    CHECK(revoked == E_OK && reregistered == E_OK && callback_info.id == info->id);
    CHECK(callback_info.features & AES_FEATURE_WINDOW);
    CHECK(state.handle == 0x7d7d && state.reserved == 0x7d7d);
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_wind_get((WORD)handle, &state) == EIHNDL);
    CHECK(aes_wind_get((WORD)extra, &state) == EIHNDL);
    CHECK(aes_wind_get((WORD)peer, &state) == EACCDN);
    /* Leave a live, text-bearing window for desktop restart cleanup. */
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && copied_text((WORD)handle) == 0);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
    (void)Cconws("x32 window events/ownership/lifecycle: PASS\r\n");
    return 0;
}
